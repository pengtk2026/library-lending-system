/**
 * @file daos.cpp
 * @brief 数据访问层实现：四个 DAO 的 SQL 语句、参数绑定与「结果行 → 领域对象」映射。
 *
 * @section design 设计说明
 * - 所属层次：数据访问层（DAO），是唯一允许出现 SQL 的层；
 *   对应声明 include/daos.h，SQL 与列清单集中在匿名命名空间，避免散落到业务/界面代码。
 * - 继承与复用：四个 DAO 公有继承模板基类 Dao<T>（见 daos.h），
 *   统一 insert / update / removeById / findById / listAll 契约；
 *   requirePersisted() 复用「更新前必须已持久化」的守卫逻辑。
 * - 映射算法：每个 DAO 的查询都以「列清单常量 + WHERE/ORDER BY 后缀」拼出 SQL，
 *   再由对应的 mapXxx(Statement&) 按固定列序把一行结果还原为实体。
 *   列序即契约：改动 SELECT 列表必须同步修改映射函数，否则会出现字段错位。
 * - 安全与性能：
 *   1) 所有用户输入均通过 ? 占位符绑定，绝不字符串拼接，杜绝 SQL 注入；
 *   2) 模糊检索用 `%keyword%` 绑定同一参数三次，空关键字由业务层直接走 listAll() 避免全表 LIKE；
 *   3) 借阅明细直接查 v_loan_detail 视图，一次 JOIN 出界面所需全部列，避免 N+1 查询；
 *   4) 批量写操作可用 Statement::reset() 复用预编译语句（见 demo_data.cpp）。
 * - 异常约定：findById 无结果抛 NotFoundException；SQL 层错误由 Statement/Database 抛 DbException；
 *   DAO 不吞异常，交由 LibraryService 与 UI 统一处理。
 *
 * @author 课程设计组
 * @date 2026-11-12
 */

#include "daos.h"

#include <string>    // std::string：SQL 拼接与关键字处理

namespace lls {
namespace {  // 匿名命名空间：SQL 常量与映射函数仅本编译单元可见（信息隐藏）

/** @brief 图书查询的标准列清单（10 列），别名 b 便于后续 JOIN 与 ORDER BY。 */
const char* const kBookColumns =
    "SELECT b.id,b.isbn,b.title,b.author,b.publisher,b.publish_year,"
    "b.category,b.shelf_id,b.status,b.added_at FROM books b ";
/** @brief 学生查询的标准列清单（7 列）。 */
const char* const kStudentColumns =
    "SELECT id,student_no,name,major,phone,max_borrow,active FROM students ";
/** @brief 书架查询的标准列清单（6 列）。 */
const char* const kShelfColumns =
    "SELECT id,code,location,category,capacity,remark FROM bookshelves ";
/** @brief 借阅记录查询的标准列清单（8 列）。 */
const char* const kLoanColumns =
    "SELECT id,student_id,book_id,borrow_date,due_date,return_date,status,remark "
    "FROM borrow_records ";
/** @brief 借阅明细视图列清单（13 列），供借阅管理页一次取齐展示字段。 */
const char* const kDetailColumns =
    "SELECT record_id,student_id,student_no,student_name,book_id,isbn,title,"
    "shelf_code,borrow_date,due_date,return_date,status,remark FROM v_loan_detail ";

/**
 * @brief 把查询结果当前行映射为 Book 实体。
 * @param statement 已 step() 到 SQLITE_ROW 的语句。
 * @return 值语义的 Book 对象。
 * @note 算法：严格按 kBookColumns 的列序读取；status 为整数，需经 statusFromInt 安全转换。
 */
Book mapBook(Statement& statement) {
    Book book;                                                ///< 待填充的图书实体
    book.setId(statement.columnInt(0));                       ///< 列 0：主键
    book.setIsbn(statement.columnText(1));                    ///< 列 1：ISBN
    book.setTitle(statement.columnText(2));                   ///< 列 2：题名
    book.setAuthor(statement.columnText(3));                  ///< 列 3：作者
    book.setPublisher(statement.columnText(4));               ///< 列 4：出版社
    book.setPublishYear(statement.columnInt(5));              ///< 列 5：出版年
    book.setCategory(statement.columnText(6));                ///< 列 6：分类
    book.setShelfId(statement.columnInt(7));                  ///< 列 7：书架外键
    book.setStatus(Book::statusFromInt(statement.columnInt(8)));  ///< 列 8：状态整数→枚举
    book.setAddedAt(statement.columnText(9));                 ///< 列 9：入库日期
    return book;
}

/**
 * @brief 把当前行映射为 Student 实体。
 * @param statement 已定位到结果行的语句。
 * @return Student 对象。
 * @note active 列是 0/1 整数，转换为 bool；max_borrow 直接读整数。
 */
Student mapStudent(Statement& statement) {
    Student student;                                          ///< 待填充的学生实体
    student.setId(statement.columnInt(0));                    ///< 列 0：主键
    student.setStudentNo(statement.columnText(1));            ///< 列 1：学号
    student.setName(statement.columnText(2));                 ///< 列 2：姓名
    student.setMajor(statement.columnText(3));                ///< 列 3：专业班级
    student.setPhone(statement.columnText(4));                ///< 列 4：电话
    student.setMaxBorrow(statement.columnInt(5));             ///< 列 5：可借上限
    student.setActive(statement.columnInt(6) != 0);          ///< 列 6：0/1 → bool
    return student;
}

/**
 * @brief 把当前行映射为 Bookshelf 实体。
 * @param statement 已定位到结果行的语句。
 * @return Bookshelf 对象。
 * @note bookIds_ 属于内存聚合，不在此填充；由 countBooks()/listWithStats() 另行查询。
 */
Bookshelf mapShelf(Statement& statement) {
    Bookshelf shelf;                                          ///< 待填充的书架实体
    shelf.setId(statement.columnInt(0));                      ///< 列 0：主键
    shelf.setCode(statement.columnText(1));                   ///< 列 1：编号
    shelf.setLocation(statement.columnText(2));               ///< 列 2：位置
    shelf.setCategory(statement.columnText(3));               ///< 列 3：分类
    shelf.setCapacity(statement.columnInt(4));                ///< 列 4：容量
    shelf.setRemark(statement.columnText(5));                 ///< 列 5：备注
    return shelf;
}

/**
 * @brief 把当前行映射为 BorrowRecord 实体。
 * @param statement 已定位到结果行的语句。
 * @return BorrowRecord 对象。
 * @note 算法：return_date 允许 SQL NULL，先 columnIsNull 判断再决定是否 setReturnDate；
 *       未归还时 returnDate_ 保持空串，与「SQL NULL」语义对应。
 */
BorrowRecord mapLoan(Statement& statement) {
    BorrowRecord record;                                      ///< 待填充的借阅记录
    record.setId(statement.columnInt(0));                     ///< 列 0：记录主键
    record.setStudentId(statement.columnInt(1));              ///< 列 1：学生外键
    record.setBookId(statement.columnInt(2));                 ///< 列 2：图书外键
    record.setBorrowDate(statement.columnText(3));            ///< 列 3：借出日
    record.setDueDate(statement.columnText(4));               ///< 列 4：应还日
    if (!statement.columnIsNull(5))
        record.setReturnDate(statement.columnText(5));        ///< 列 5：归还日（可空）
    record.setStatus(BorrowRecord::statusFromInt(
        statement.columnInt(6)));                             ///< 列 6：状态整数→枚举
    record.setRemark(statement.columnText(7));                ///< 列 7：备注
    return record;
}

/**
 * @brief 把视图当前行映射为 LoanDetail（含领域对象 + 展示字段）。
 * @param statement 已定位到 v_loan_detail 结果行的语句。
 * @return LoanDetail 对象。
 * @note 算法：把 13 列拆成两类——属于 BorrowRecord 的列写入 record，
 *       属于 JOIN 得到的学生/图书/书架展示字段写入 LoanDetail 的公有字段，
 *       界面因此无需二次查询即可渲染完整借阅列表。
 */
LoanDetail mapDetail(Statement& statement) {
    LoanDetail detail;                                        ///< 返回的展示结构体
    BorrowRecord record;                                      ///< 内嵌的领域对象
    record.setId(statement.columnInt(0));                     ///< 列 0：记录号
    record.setStudentId(statement.columnInt(1));              ///< 列 1：学生 ID
    detail.studentNo = statement.columnText(2);               ///< 列 2：学号（展示）
    detail.studentName = statement.columnText(3);             ///< 列 3：姓名（展示）
    record.setBookId(statement.columnInt(4));                 ///< 列 4：图书 ID
    detail.isbn = statement.columnText(5);                    ///< 列 5：ISBN（展示）
    detail.bookTitle = statement.columnText(6);               ///< 列 6：题名（展示）
    detail.shelfCode = statement.columnText(7);               ///< 列 7：书架号（展示）
    record.setBorrowDate(statement.columnText(8));            ///< 列 8：借出日
    record.setDueDate(statement.columnText(9));               ///< 列 9：应还日
    if (!statement.columnIsNull(10))
        record.setReturnDate(statement.columnText(10));       ///< 列 10：归还日（可空）
    record.setStatus(BorrowRecord::statusFromInt(
        statement.columnInt(11)));                            ///< 列 11：状态
    record.setRemark(statement.columnText(12));               ///< 列 12：备注
    detail.record = record;                                   ///< 组装为完整展示对象
    return detail;
}

}  // namespace 匿名命名空间结束

// ==================== BookDao（books 表） ====================

/**
 * @brief 插入一册图书。
 * @param book 待插入实体（id 应为 0）。
 * @return 新记录的自增主键。
 * @note 9 个占位符与 books 列一一对应；added_at 为空时用领域层 today() 兜底。
 */
int BookDao::insert(const Book& book) {
    Statement statement = db_.prepare(                        ///< 预编译 INSERT 语句
        "INSERT INTO books(isbn,title,author,publisher,publish_year,category,"
        "shelf_id,status,added_at) VALUES(?,?,?,?,?,?,?,?,?);");
    statement.bindText(1, book.isbn())                       ///< 1：ISBN
             .bindText(2, book.title())                      ///< 2：题名
             .bindText(3, book.author())                     ///< 3：作者
             .bindText(4, book.publisher())                  ///< 4：出版社
             .bindInt(5, book.publishYear())                 ///< 5：出版年
             .bindText(6, book.category())                   ///< 6：分类
             .bindInt(7, book.shelfId())                     ///< 7：书架外键
             .bindInt(8, static_cast<int>(book.status()))    ///< 8：状态枚举→整数
             .bindText(9, book.addedAt().empty()
                              ? today() : book.addedAt());   ///< 9：入库日期（空则取今天）
    statement.execute();                                     ///< 执行到完成
    return static_cast<int>(db_.lastInsertId());             ///< 返回新主键
}

/**
 * @brief 全字段更新图书。
 * @param book 含有效 id 的实体。
 * @note 先 requirePersisted 守卫 id > 0；最后一列为 WHERE id=?，防止全表更新。
 */
void BookDao::update(const Book& book) {
    requirePersisted(book.id());                             ///< 更新必须带主键
    Statement statement = db_.prepare(                        ///< 预编译 UPDATE 语句
        "UPDATE books SET isbn=?,title=?,author=?,publisher=?,publish_year=?,"
        "category=?,shelf_id=?,status=?,added_at=? WHERE id=?;");
    statement.bindText(1, book.isbn())                       ///< 1：ISBN
             .bindText(2, book.title())                      ///< 2：题名
             .bindText(3, book.author())                     ///< 3：作者
             .bindText(4, book.publisher())                  ///< 4：出版社
             .bindInt(5, book.publishYear())                 ///< 5：出版年
             .bindText(6, book.category())                   ///< 6：分类
             .bindInt(7, book.shelfId())                     ///< 7：书架外键
             .bindInt(8, static_cast<int>(book.status()))    ///< 8：状态
             .bindText(9, book.addedAt().empty()
                              ? today() : book.addedAt())    ///< 9：入库日期
             .bindInt(10, book.id());                        ///< 10：WHERE 主键
    statement.execute();
}

/**
 * @brief 按主键删除图书。
 * @param id 图书主键。
 * @return 是否真的删除了记录。
 * @note 删除是否成功由 sqlite3_changes() 判断，避免「删了不存在的 ID 却返回成功」。
 */
bool BookDao::removeById(int id) {
    Statement statement = db_.prepare("DELETE FROM books WHERE id=?;");  ///< 删除语句
    statement.bindInt(1, id);                                ///< 1：主键
    statement.execute();
    return db_.changes() > 0;                                ///< 影响行数 > 0 才算成功
}

/**
 * @brief 按主键查询单册图书。
 * @param id 图书主键。
 * @return Book 实体。
 * @exception NotFoundException 记录不存在时抛出。
 */
Book BookDao::findById(int id) const {
    Statement statement = db_.prepare(
        std::string(kBookColumns) + "WHERE b.id=?;");        ///< 列清单 + 主键条件
    statement.bindInt(1, id);                                ///< 1：主键
    if (!statement.step())
        throw NotFoundException("图书 ID=" + std::to_string(id) + " 不存在");
    return mapBook(statement);                               ///< 映射为领域对象
}

/** @brief 列出全部图书，按主键升序，保证界面顺序稳定。 */
std::vector<Book> BookDao::listAll() const {
    Statement statement = db_.prepare(
        std::string(kBookColumns) + "ORDER BY b.id;");       ///< 全表查询
    std::vector<Book> result;                                ///< 结果容器（值语义）
    while (statement.step()) result.push_back(mapBook(statement));  ///< 逐行映射
    return result;
}

/**
 * @brief 按题名 / 作者 / ISBN 模糊检索。
 * @param keyword 用户输入的关键字（可含首尾空白）。
 * @return 匹配图书列表。
 * @note 主要算法：把 trim(keyword) 包成 `%kw%`，同一参数绑定给三个 LIKE；
 *       业务层保证空关键字不会调用本方法（改走 listAll），从而避免无谓的全表扫描。
 *       JOIN bookshelves 仅为与设计文档的表关联口径一致，SELECT 仍只取 b.* 列。
 */
std::vector<Book> BookDao::search(const std::string& keyword) const {
    Statement statement = db_.prepare(                        ///< 预编译三列 LIKE 查询
        std::string(kBookColumns) +
        "LEFT JOIN bookshelves sh ON sh.id=b.shelf_id "
        "WHERE b.title LIKE ? OR b.author LIKE ? OR b.isbn LIKE ? ORDER BY b.id;");
    std::string pattern = "%" + trim(keyword) + "%";        ///< 模糊匹配模式
    statement.bindText(1, pattern)                           ///< 题名 LIKE
             .bindText(2, pattern)                           ///< 作者 LIKE
             .bindText(3, pattern);                          ///< ISBN LIKE
    std::vector<Book> result;                                ///< 命中结果
    while (statement.step()) result.push_back(mapBook(statement));
    return result;
}

/** @brief 仅列出「在馆」（status=0）图书，用于借书页下拉框。 */
std::vector<Book> BookDao::availableList() const {
    Statement statement = db_.prepare(
        std::string(kBookColumns) + "WHERE b.status=0 ORDER BY b.id;");  ///< 状态过滤
    std::vector<Book> result;                                ///< 在馆图书集合
    while (statement.step()) result.push_back(mapBook(statement));
    return result;
}

/**
 * @brief 仅更新图书状态（借出/归还时调用）。
 * @param id 图书主键。
 * @param status 目标状态。
 * @note 该语句与 borrow_records 的插入/更新必须处于同一事务，由 LibraryService 保证。
 */
void BookDao::updateStatus(int id, BookStatus status) {
    Statement statement = db_.prepare("UPDATE books SET status=? WHERE id=?;");  ///< 单列更新
    statement.bindInt(1, static_cast<int>(status))           ///< 1：状态枚举转整数
             .bindInt(2, id);                                ///< 2：主键
    statement.execute();
}

/** @brief 统计指定书架上的藏书数（删除书架前的引用预检）。 */
int BookDao::countByShelf(int shelfId) const {
    Statement statement = db_.prepare(
        "SELECT COUNT(*) FROM books WHERE shelf_id=?;");     ///< 聚合查询
    statement.bindInt(1, shelfId);                           ///< 1：书架 ID
    return statement.step() ? statement.columnInt(0) : 0;   ///< 无行时返回 0
}

// ==================== StudentDao（students 表） ====================

/** @brief 插入学生，返回新主键；active 布尔值转 0/1 存储。 */
int StudentDao::insert(const Student& student) {
    Statement statement = db_.prepare(                        ///< 预编译 INSERT
        "INSERT INTO students(student_no,name,major,phone,max_borrow,active) "
        "VALUES(?,?,?,?,?,?);");
    statement.bindText(1, student.studentNo())               ///< 1：学号（UNIQUE）
             .bindText(2, student.name())                    ///< 2：姓名
             .bindText(3, student.major())                   ///< 3：专业
             .bindText(4, student.phone())                   ///< 4：电话
             .bindInt(5, student.maxBorrow())                ///< 5：可借上限
             .bindInt(6, student.active() ? 1 : 0);          ///< 6：启用标记
    statement.execute();
    return static_cast<int>(db_.lastInsertId());             ///< 新主键
}

/** @brief 全字段更新学生，需带有效主键。 */
void StudentDao::update(const Student& student) {
    requirePersisted(student.id());                          ///< 主键守卫
    Statement statement = db_.prepare(                        ///< 预编译 UPDATE
        "UPDATE students SET student_no=?,name=?,major=?,phone=?,max_borrow=?,"
        "active=? WHERE id=?;");
    statement.bindText(1, student.studentNo())               ///< 1：学号
             .bindText(2, student.name())                    ///< 2：姓名
             .bindText(3, student.major())                   ///< 3：专业
             .bindText(4, student.phone())                   ///< 4：电话
             .bindInt(5, student.maxBorrow())                ///< 5：可借上限
             .bindInt(6, student.active() ? 1 : 0)           ///< 6：启用标记
             .bindInt(7, student.id());                      ///< 7：WHERE 主键
    statement.execute();
}

/** @brief 按主键删除学生，返回是否命中。 */
bool StudentDao::removeById(int id) {
    Statement statement = db_.prepare("DELETE FROM students WHERE id=?;");  ///< 删除语句
    statement.bindInt(1, id);                                ///< 1：主键
    statement.execute();
    return db_.changes() > 0;                                ///< 影响行数判定
}

/** @brief 按主键查询学生；不存在抛 NotFoundException。 */
Student StudentDao::findById(int id) const {
    Statement statement = db_.prepare(
        std::string(kStudentColumns) + "WHERE id=?;");       ///< 列清单 + 条件
    statement.bindInt(1, id);                                ///< 1：主键
    if (!statement.step())
        throw NotFoundException("学生 ID=" + std::to_string(id) + " 不存在");
    return mapStudent(statement);                            ///< 映射实体
}

/** @brief 列出全部学生，按主键升序。 */
std::vector<Student> StudentDao::listAll() const {
    Statement statement = db_.prepare(
        std::string(kStudentColumns) + "ORDER BY id;");      ///< 全表查询
    std::vector<Student> result;                             ///< 结果容器
    while (statement.step()) result.push_back(mapStudent(statement));
    return result;
}

/**
 * @brief 按学号精确查询。
 * @param no 学号。
 * @param out 输出参数：命中时写入实体。
 * @return 是否命中（用返回值而非异常，因为「查不到」在此是正常分支）。
 */
bool StudentDao::findByStudentNo(const std::string& no, Student& out) const {
    Statement statement = db_.prepare(
        std::string(kStudentColumns) + "WHERE student_no=?;");  ///< 唯一索引查询
    statement.bindText(1, no);                               ///< 1：学号
    if (!statement.step()) return false;                     ///< 未命中
    out = mapStudent(statement);                             ///< 回填输出参数
    return true;
}

/**
 * @brief 判断学号是否已被「其它记录」占用。
 * @param no 待检查学号。
 * @param excludeId 编辑时排除的自身 ID（新增时传 0）。
 * @return true 表示重复。
 * @note 算法：COUNT(*) ... AND id <> ?，用排除自身的方式让新增与编辑共用同一检查逻辑。
 */
bool StudentDao::existsNo(const std::string& no, int excludeId) const {
    Statement statement = db_.prepare(
        "SELECT COUNT(*) FROM students WHERE student_no=? AND id<>?;");  ///< 排除自身计数
    statement.bindText(1, no)                                ///< 1：学号
             .bindInt(2, excludeId);                         ///< 2：排除的 ID
    return statement.step() && statement.columnInt(0) > 0;  ///< 计数 > 0 即重复
}

/**
 * @brief 统计学生当前未归还的借阅册数。
 * @param studentId 学生主键。
 * @return 未归还记录数（status IN (0,2)）。
 * @note 该数是业务层判断「是否达到可借上限」的输入；索引 idx_loans_student 加速此查询。
 */
int StudentDao::countActiveLoans(int studentId) const {
    Statement statement = db_.prepare(
        "SELECT COUNT(*) FROM borrow_records WHERE student_id=? AND status IN (0,2);");
    statement.bindInt(1, studentId);                         ///< 1：学生 ID
    return statement.step() ? statement.columnInt(0) : 0;   ///< 无行返回 0
}

// ==================== BookshelfDao（bookshelves 表） ====================

/** @brief 插入书架，返回新主键。 */
int BookshelfDao::insert(const Bookshelf& shelf) {
    Statement statement = db_.prepare(                        ///< 预编译 INSERT
        "INSERT INTO bookshelves(code,location,category,capacity,remark) "
        "VALUES(?,?,?,?,?);");
    statement.bindText(1, shelf.code())                      ///< 1：编号（UNIQUE）
             .bindText(2, shelf.location())                  ///< 2：位置
             .bindText(3, shelf.category())                  ///< 3：分类
             .bindInt(4, shelf.capacity())                   ///< 4：容量
             .bindText(5, shelf.remark());                   ///< 5：备注
    statement.execute();
    return static_cast<int>(db_.lastInsertId());             ///< 新主键
}

/** @brief 全字段更新书架，需带有效主键。 */
void BookshelfDao::update(const Bookshelf& shelf) {
    requirePersisted(shelf.id());                            ///< 主键守卫
    Statement statement = db_.prepare(                        ///< 预编译 UPDATE
        "UPDATE bookshelves SET code=?,location=?,category=?,capacity=?,remark=? "
        "WHERE id=?;");
    statement.bindText(1, shelf.code())                      ///< 1：编号
             .bindText(2, shelf.location())                  ///< 2：位置
             .bindText(3, shelf.category())                  ///< 3：分类
             .bindInt(4, shelf.capacity())                   ///< 4：容量
             .bindText(5, shelf.remark())                    ///< 5：备注
             .bindInt(6, shelf.id());                        ///< 6：WHERE 主键
    statement.execute();
}

/** @brief 按主键删除书架，返回是否命中（外键 RESTRICT 可能使其失败并抛 DbException）。 */
bool BookshelfDao::removeById(int id) {
    Statement statement = db_.prepare("DELETE FROM bookshelves WHERE id=?;");  ///< 删除语句
    statement.bindInt(1, id);                                ///< 1：主键
    statement.execute();
    return db_.changes() > 0;                                ///< 影响行数判定
}

/** @brief 按主键查询书架；不存在抛 NotFoundException。 */
Bookshelf BookshelfDao::findById(int id) const {
    Statement statement = db_.prepare(
        std::string(kShelfColumns) + "WHERE id=?;");         ///< 列清单 + 条件
    statement.bindInt(1, id);                                ///< 1：主键
    if (!statement.step())
        throw NotFoundException("书架 ID=" + std::to_string(id) + " 不存在");
    return mapShelf(statement);                              ///< 映射实体
}

/** @brief 列出全部书架，按主键升序。 */
std::vector<Bookshelf> BookshelfDao::listAll() const {
    Statement statement = db_.prepare(
        std::string(kShelfColumns) + "ORDER BY id;");        ///< 全表查询
    std::vector<Bookshelf> result;                           ///< 结果容器
    while (statement.step()) result.push_back(mapShelf(statement));
    return result;
}

/**
 * @brief 判断书架编号是否已被其它记录占用。
 * @param code 编号。
 * @param excludeId 编辑时排除的自身 ID（新增传 0）。
 * @return true 表示重复。
 */
bool BookshelfDao::existsCode(const std::string& code, int excludeId) const {
    Statement statement = db_.prepare(
        "SELECT COUNT(*) FROM bookshelves WHERE code=? AND id<>?;");  ///< 排除自身计数
    statement.bindText(1, code)                              ///< 1：编号
             .bindInt(2, excludeId);                         ///< 2：排除的 ID
    return statement.step() && statement.columnInt(0) > 0;  ///< 计数 > 0 即重复
}

/** @brief 统计书架上的藏书数，用于删除预检（业务层给出友好提示而非让外键报错）。 */
int BookshelfDao::countBooks(int shelfId) const {
    Statement statement = db_.prepare(
        "SELECT COUNT(*) FROM books WHERE shelf_id=?;");     ///< 聚合查询
    statement.bindInt(1, shelfId);                           ///< 1：书架 ID
    return statement.step() ? statement.columnInt(0) : 0;   ///< 无行返回 0
}

/**
 * @brief 读取书架统计视图，返回「书架 + 藏书数 + 在馆数」的组合结构。
 * @return ShelfStat 列表。
 * @note 算法：直接查询 v_shelf_stat（LEFT JOIN + GROUP BY 已在 SQL 层完成聚合），
 *       避免对每个书架单独 COUNT 造成 N+1 查询；视图已包含 remark 列以保持行完整。
 */
std::vector<ShelfStat> BookshelfDao::listWithStats() const {
    Statement statement = db_.prepare(                        ///< 视图查询
        "SELECT id,code,location,category,capacity,remark,book_count,available_count "
        "FROM v_shelf_stat ORDER BY id;");
    std::vector<ShelfStat> result;                           ///< 结果容器
    while (statement.step()) {
        ShelfStat stat;                                      ///< 单条统计结果
        stat.shelf.setId(statement.columnInt(0));            ///< 列 0：主键
        stat.shelf.setCode(statement.columnText(1));         ///< 列 1：编号
        stat.shelf.setLocation(statement.columnText(2));     ///< 列 2：位置
        stat.shelf.setCategory(statement.columnText(3));     ///< 列 3：分类
        stat.shelf.setCapacity(statement.columnInt(4));      ///< 列 4：容量
        stat.shelf.setRemark(statement.columnText(5));       ///< 列 5：备注
        stat.bookCount = statement.columnInt(6);             ///< 列 6：藏书数
        stat.availableCount = statement.columnInt(7);        ///< 列 7：在馆数
        result.push_back(stat);
    }
    return result;
}

// ==================== BorrowRecordDao（borrow_records 表） ====================

/**
 * @brief 插入一条借阅记录。
 * @param record 借阅实体（return_date 应为空）。
 * @return 新记录主键。
 * @note 不写 return_date 列，让其保持 SQL NULL；status 为 0（借出中）或 2（逾期演示）。
 *       部分唯一索引 uq_active_loan 会阻止同一册书出现第二条未归还记录。
 */
int BorrowRecordDao::insert(const BorrowRecord& record) {
    Statement statement = db_.prepare(                        ///< 预编译 INSERT
        "INSERT INTO borrow_records(student_id,book_id,borrow_date,due_date,status,remark) "
        "VALUES(?,?,?,?,?,?);");
    statement.bindInt(1, record.studentId())                 ///< 1：学生外键
             .bindInt(2, record.bookId())                    ///< 2：图书外键
             .bindText(3, record.borrowDate())               ///< 3：借出日
             .bindText(4, record.dueDate())                  ///< 4：应还日
             .bindInt(5, static_cast<int>(record.status()))  ///< 5：状态
             .bindText(6, record.remark());                  ///< 6：备注
    statement.execute();
    return static_cast<int>(db_.lastInsertId());             ///< 新主键
}

/**
 * @brief 全字段更新借阅记录。
 * @param record 含有效 id 的实体。
 * @note 算法：return_date 为空串时绑定 SQL NULL（而非空字符串），
 *       以保持「未归还 = NULL」的数据库语义，与 mapLoan() 的判空逻辑对应。
 */
void BorrowRecordDao::update(const BorrowRecord& record) {
    requirePersisted(record.id());                           ///< 主键守卫
    Statement statement = db_.prepare(                        ///< 预编译 UPDATE
        "UPDATE borrow_records SET student_id=?,book_id=?,borrow_date=?,due_date=?,"
        "return_date=?,status=?,remark=? WHERE id=?;");
    statement.bindInt(1, record.studentId())                 ///< 1：学生外键
             .bindInt(2, record.bookId())                    ///< 2：图书外键
             .bindText(3, record.borrowDate())               ///< 3：借出日
             .bindText(4, record.dueDate());                 ///< 4：应还日
    if (record.returnDate().empty())
        statement.bindNull(5);                               ///< 5：未归还 → SQL NULL
    else
        statement.bindText(5, record.returnDate());          ///< 5：已归还 → 日期文本
    statement.bindInt(6, static_cast<int>(record.status()))  ///< 6：状态
             .bindText(7, record.remark())                   ///< 7：备注
             .bindInt(8, record.id());                       ///< 8：WHERE 主键
    statement.execute();
}

/** @brief 按主键删除借阅记录，返回是否命中。 */
bool BorrowRecordDao::removeById(int id) {
    Statement statement = db_.prepare("DELETE FROM borrow_records WHERE id=?;");  ///< 删除语句
    statement.bindInt(1, id);                                ///< 1：主键
    statement.execute();
    return db_.changes() > 0;                                ///< 影响行数判定
}

/** @brief 按主键查询借阅记录；不存在抛 NotFoundException。 */
BorrowRecord BorrowRecordDao::findById(int id) const {
    Statement statement = db_.prepare(
        std::string(kLoanColumns) + "WHERE id=?;");          ///< 列清单 + 条件
    statement.bindInt(1, id);                                ///< 1：主键
    if (!statement.step())
        throw NotFoundException("借阅记录 ID=" + std::to_string(id) + " 不存在");
    return mapLoan(statement);                               ///< 映射实体
}

/** @brief 列出全部借阅记录，按借出日与主键倒序（最新在前）。 */
std::vector<BorrowRecord> BorrowRecordDao::listAll() const {
    Statement statement = db_.prepare(
        std::string(kLoanColumns) +
        "ORDER BY borrow_date DESC, id DESC;");              ///< 倒序查询
    std::vector<BorrowRecord> result;                        ///< 结果容器
    while (statement.step()) result.push_back(mapLoan(statement));
    return result;
}

/**
 * @brief 办理归还：登记归还日并把状态改为已归还。
 * @param id 记录主键。
 * @param returnDate 归还日期。
 * @return 是否更新成功。
 * @note 算法：WHERE 中附加 `status<>1` 实现幂等保护，
 *       配合 changes() 让业务层能区分「成功」与「记录已归还/不存在」。
 */
bool BorrowRecordDao::close(int id, const std::string& returnDate) {
    Statement statement = db_.prepare(                        ///< 条件更新
        "UPDATE borrow_records SET return_date=?,status=1 WHERE id=? AND status<>1;");
    statement.bindText(1, returnDate)                        ///< 1：归还日
             .bindInt(2, id);                                ///< 2：主键
    statement.execute();
    return db_.changes() > 0;                                ///< 是否命中未归还记录
}

/**
 * @brief 按筛选条件查询借阅明细（供借阅管理页渲染）。
 * @param filter All / Active / Overdue。
 * @return LoanDetail 列表。
 * @note 主要算法（条件拼接）：
 *       - All：不加 WHERE，显示全部历史；
 *       - Active：`status IN (0,2)`，即未归还（含逾期）；
 *       - Overdue：`status<>1 AND due_date < date('now','localtime')`，
 *         用 SQL 的本地日期函数保证与领域层 today() 同一时区口径；
 *       最后统一按借出日、记录号倒序。因不涉及用户输入，此处拼接的是固定常量，无注入风险。
 */
std::vector<LoanDetail> BorrowRecordDao::listDetails(LoanFilter filter) const {
    std::string sql = kDetailColumns;                        ///< 基础视图列清单
    switch (filter) {
        case LoanFilter::Active:
            sql += "WHERE status IN (0,2) ";                 ///< 当前借阅（含逾期）
            break;
        case LoanFilter::Overdue:
            sql += "WHERE status<>1 AND due_date < date('now','localtime') ";  ///< 逾期条件
            break;
        case LoanFilter::All:
        default:
            break;                                           ///< 全部：不加条件
    }
    sql += "ORDER BY borrow_date DESC, record_id DESC;";     ///< 统一排序
    Statement statement = db_.prepare(sql);                  ///< 预编译最终 SQL
    std::vector<LoanDetail> result;                          ///< 结果容器
    while (statement.step()) result.push_back(mapDetail(statement));
    return result;
}

/** @brief 无参重载：查询全部借阅明细。 */
std::vector<LoanDetail> BorrowRecordDao::listDetails() const {
    return listDetails(LoanFilter::All);                     ///< 委托给带筛选参数的版本
}

/** @brief 统计学生当前未归还册数（与 StudentDao::countActiveLoans 等价，供借阅侧复用）。 */
int BorrowRecordDao::countActiveByStudent(int studentId) const {
    Statement statement = db_.prepare(
        "SELECT COUNT(*) FROM borrow_records WHERE student_id=? AND status IN (0,2);");
    statement.bindInt(1, studentId);                         ///< 1：学生 ID
    return statement.step() ? statement.columnInt(0) : 0;   ///< 无行返回 0
}

/**
 * @brief 判断学生是否存在逾期未还记录。
 * @param studentId 学生主键。
 * @param day 判定基准日（业务层传 today()）。
 * @return true 表示存在逾期。
 * @note 算法：不依赖 status=2 的标记是否已被刷新，直接比较 due_date < day，
 *       因此即使尚未执行批量逾期标记，也能正确拦截借书请求。
 */
bool BorrowRecordDao::hasOverdue(int studentId, const std::string& day) const {
    Statement statement = db_.prepare(                        ///< 逾期计数查询
        "SELECT COUNT(*) FROM borrow_records WHERE student_id=? AND status<>1 "
        "AND due_date < ?;");
    statement.bindInt(1, studentId)                          ///< 1：学生 ID
             .bindText(2, day);                              ///< 2：今天
    return statement.step() && statement.columnInt(0) > 0;  ///< 计数 > 0 即逾期
}

/** @brief 判断某册图书是否存在未归还记录（删除图书前的引用预检）。 */
bool BorrowRecordDao::activeForBook(int bookId) const {
    Statement statement = db_.prepare(
        "SELECT COUNT(*) FROM borrow_records WHERE book_id=? AND status IN (0,2);");
    statement.bindInt(1, bookId);                            ///< 1：图书 ID
    return statement.step() && statement.columnInt(0) > 0;  ///< 计数 > 0 即被占用
}

/**
 * @brief 批量把到期未还的正常借阅标记为逾期。
 * @param day 判定基准日。
 * @return 被更新的行数，供状态栏反馈。
 * @note 主要算法：只处理 status=0（借出中）且 due_date < day 的记录，
 *       已归还(1)与已逾期(2)不受影响，因此可安全地在每次刷新时重复执行（幂等）。
 */
int BorrowRecordDao::markOverdue(const std::string& day) {
    Statement statement = db_.prepare(
        "UPDATE borrow_records SET status=2 WHERE status=0 AND due_date < ?;");  ///< 批量更新
    statement.bindText(1, day);                              ///< 1：今天
    statement.execute();
    return db_.changes();                                    ///< 影响行数
}

}  // namespace lls
