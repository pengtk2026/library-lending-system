/**
 * @file service.cpp
 * @brief 业务逻辑层实现：业务规则校验、事务边界编排、组合查询与统计。
 *
 * @par 设计说明
 * - 所属层次：业务逻辑层（Service），是界面层唯一允许调用的下层门面；
 *   对应声明 include/service.h，界面层不得直接访问 DAO，更不能出现 SQL。
 * - 依赖注入：构造函数接收 Database&，并把同一连接注入四个 DAO；
 *   Database 的生命周期由 main() 中的栈对象管理，Service 不持有其所有权。
 * - 职责分工（单一职责）：
 *   1) 领域对象负责「不变式」（validate）与「状态迁移规则」（canBorrowMore / isAvailable）；
 *   2) DAO 负责「取数与写库」；
 *   3) 本层负责「跨对象的流程编排 + 事务边界 + 面向用户的业务文案」。
 * - 异常约定：本层抛出 ValidationException / NotFoundException / BusinessException，
 *   UI 回调统一捕获并转换为对话框与状态栏提示，异常绝不穿透 FLTK 回调。
 * - 事务边界（最关键的设计）：借书与还书都必须在**同一事务**内同时更新
 *   borrow_records 与 books.status；事务对象在栈上，异常栈展开时自动回滚，
 *   因此不会出现「有记录但书仍在馆」或「书已借出但无记录」的中间态。
 *
 * @author 课程设计组
 * @date 2026-11-12
 */

#include "service.h"

#include "database.h"   // Transaction / Statement：事务与聚合查询

#include <sstream>      // std::ostringstream：统计条文本格式化

namespace lls {

/*
 * @brief 把统计结果格式化为主窗口顶部统计条文本。
 * @return 形如「藏书 1000 册 在馆 800 借出 200 逾期 40 学生 100 书架 20」。
 * @note 用流式拼接避免多处字符串加法；字段顺序与界面控件宽度设计一致。
 */
std::string Statistics::summary() const {
    std::ostringstream out;                                  ///< 文本流，逐项追加
    out << "藏书 " << totalBooks << " 册   在馆 " << availableBooks
        << "   借出 " << borrowedBooks << "   逾期 " << overdueLoans
        << "   学生 " << totalStudents << "   书架 " << totalShelves;
    return out.str();                                        ///< 转为值语义字符串
}

/*
 * @brief 通用实体校验入口（多态落点）。
 * @param entity 任意实体基类引用。
 * @exception ValidationException 实体返回非空错误文本时抛出。
 * @note 主要算法：通过 `const Entity&` 调用虚函数 validate()，
 *       运行时动态绑定到 Book/Student/Bookshelf/BorrowRecord 的各自实现；
 *       领域层只返回错误文案，由本层统一决定「转成异常」这一策略。
 */
void LibraryService::checkEntity(const Entity& entity) {
    const std::string error = entity.validate();             ///< 多态校验，空串代表合法
    if (!error.empty()) throw ValidationException(error);    ///< 统一转成异常
}

/*
 * @brief 构造业务门面并注入数据库连接。
 * @param db 已打开的连接引用；Service 不接管其生命周期。
 * @note 四个 DAO 共享同一连接，保证后续事务（BEGIN/COMMIT）对所有 DAO 都生效。
 */
LibraryService::LibraryService(Database& db)
    : db_(db),                                              ///< 连接引用：事务与统计使用
      bookDao_(db),                                         ///< 图书 DAO
      studentDao_(db),                                      ///< 学生 DAO
      shelfDao_(db),                                        ///< 书架 DAO
      loanDao_(db) {}                                       ///< 借阅记录 DAO

/*
 * @brief 系统初始化：建表 → 空库播种 → 批量刷新逾期。
 * @note 算法顺序不可颠倒：先建表才能查询；seedDemoData 内部判定是否为空库；
 *       refreshOverdue 放最后，确保演示数据中的逾期记录被正确标记。
 */
void LibraryService::initialize() {
    DbInitializer::createSchema(db_);                        ///< 幂等建表/索引/视图
    DbInitializer::seedDemoData(db_);                        ///< 空库时写入基础演示数据
    refreshOverdue();                                        ///< 启动时刷新逾期状态
}

// ==================== 图书业务 ====================

/*
 * @brief 新增图书。
 * @param book 界面采集的图书实体。
 * @return 新记录主键。
 * @note 算法：多态校验 → 书架外键预检（给出「书架不存在」而非 SQLite 报错）→
 *       补默认入库日 → 插入。校验与预检都在写库之前，减少无效事务。
 */
int LibraryService::addBook(const Book& book) {
    checkEntity(book);                                       ///< 1) 字段不变式
    shelfDao_.findById(book.shelfId());                      ///< 2) 外键预检
    Book entity = book;                                      ///< 3) 值语义副本，便于补默认值
    if (entity.addedAt().empty()) entity.setAddedAt(today());///< 4) 入库日缺省取今天
    return bookDao_.insert(entity);                          ///< 5) 写入并返回主键
}

/*
 * @brief 更新图书。
 * @param book 含有效 id 的实体。
 * @note 算法：校验 → 主键合法性 → 确认原记录存在 → 确认新书架存在 → 更新。
 */
void LibraryService::updateBook(const Book& book) {
    checkEntity(book);
    if (book.id() <= 0) throw ValidationException("图书 ID 无效");
    bookDao_.findById(book.id());                            ///< 防止更新已删除的记录
    shelfDao_.findById(book.shelfId());                      ///< 外键预检
    bookDao_.update(book);
}

/*
 * @brief 删除图书（带引用预检）。
 * @param id 图书主键。
 * @exception BusinessException 存在未归还借阅记录时抛出。
 * @note 算法：双重判定——DAO 查未归还记录 + 领域对象状态是否为「借出」，
 *       任一为真即拒绝；数据库外键 RESTRICT 作为最后防线。
 */
void LibraryService::removeBook(int id) {
    Book book = bookDao_.findById(id);                        ///< 先确认记录存在
    if (loanDao_.activeForBook(id) ||
        book.status() == BookStatus::Borrowed)
        throw BusinessException("该图书存在未归还借阅记录，无法删除");
    if (!bookDao_.removeById(id)) throw NotFoundException("图书不存在");
}

/*
 * @brief 列出图书，可按关键字检索。
 * @param keyword 题名/作者/ISBN 关键字；空串表示全部。
 * @return 图书列表。
 * @note 主要算法（查询分流）：先 trim，空关键字直接走 listAll() 的全表顺序扫描，
 *       避免 `% %` 形式的 LIKE 造成无谓的全表过滤；非空才走 search() 的三列 LIKE。
 */
std::vector<Book> LibraryService::listBooks(const std::string& keyword) {
    return trim(keyword).empty() ? bookDao_.listAll()
                                 : bookDao_.search(keyword);
}

/* @brief 列出所有在馆图书，供借书页「在馆图书」下拉框使用。 */
std::vector<Book> LibraryService::availableBooks() {
    return bookDao_.availableList();
}

// ==================== 学生业务 ====================

/*
 * @brief 新增学生。
 * @param student 待新增实体。
 * @return 新主键。
 * @note 算法：校验 → 学号唯一性预检（excludeId=0）→ 插入。
 *       业务预检先于数据库 UNIQUE 约束，目的是给出可读的中文提示。
 */
int LibraryService::addStudent(const Student& student) {
    checkEntity(student);
    if (studentDao_.existsNo(student.studentNo(), 0))
        throw BusinessException("学号已存在：" + student.studentNo());
    return studentDao_.insert(student);
}

/*
 * @brief 更新学生。
 * @param student 含有效 id 的实体。
 * @note 唯一性检查时排除自身 ID，否则编辑不改学号也会误报重复。
 */
void LibraryService::updateStudent(const Student& student) {
    checkEntity(student);
    if (student.id() <= 0) throw ValidationException("学生 ID 无效");
    studentDao_.findById(student.id());                      ///< 确认记录存在
    if (studentDao_.existsNo(student.studentNo(), student.id()))
        throw BusinessException("学号已存在：" + student.studentNo());
    studentDao_.update(student);
}

/*
 * @brief 删除学生。
 * @param id 学生主键。
 * @exception BusinessException 尚有未归还图书时拒绝删除（历史已归还记录不阻止删除）。
 */
void LibraryService::removeStudent(int id) {
    studentDao_.findById(id);
    if (studentDao_.countActiveLoans(id) > 0)
        throw BusinessException("该学生尚有未归还图书，无法删除");
    if (!studentDao_.removeById(id)) throw NotFoundException("学生不存在");
}

/* @brief 列出全部学生。 */
std::vector<Student> LibraryService::listStudents() {
    return studentDao_.listAll();
}

/*
 * @brief 查询某学生当前已借册数（界面「已借册数」列的数据源）。
 * @param studentId 学生主键。
 * @return 未归还记录数。
 */
int LibraryService::activeLoanCount(int studentId) {
    return loanDao_.countActiveByStudent(studentId);
}

// ==================== 书架业务 ====================

/* @brief 新增书架：校验 → 编号唯一性预检 → 插入。 */
int LibraryService::addShelf(const Bookshelf& shelf) {
    checkEntity(shelf);
    if (shelfDao_.existsCode(shelf.code(), 0))
        throw BusinessException("书架编号已存在：" + shelf.code());
    return shelfDao_.insert(shelf);
}

/* @brief 更新书架：唯一性检查排除自身 ID。 */
void LibraryService::updateShelf(const Bookshelf& shelf) {
    checkEntity(shelf);
    if (shelf.id() <= 0) throw ValidationException("书架 ID 无效");
    shelfDao_.findById(shelf.id());
    if (shelfDao_.existsCode(shelf.code(), shelf.id()))
        throw BusinessException("书架编号已存在：" + shelf.code());
    shelfDao_.update(shelf);
}

/*
 * @brief 删除书架：书架上仍有藏书时拒绝。
 * @param id 书架主键。
 * @note 先做业务预检再删除，避免直接触发外键 RESTRICT 抛出难以理解的 DbException。
 */
void LibraryService::removeShelf(int id) {
    shelfDao_.findById(id);
    if (shelfDao_.countBooks(id) > 0)
        throw BusinessException("该书架上仍有藏书，无法删除");
    if (!shelfDao_.removeById(id)) throw NotFoundException("书架不存在");
}

/* @brief 列出全部书架（供图书对话框的下拉选项使用）。 */
std::vector<Bookshelf> LibraryService::listShelves() {
    return shelfDao_.listAll();
}

/* @brief 读取书架统计（藏书数/在馆数），供书架管理页列表展示。 */
std::vector<ShelfStat> LibraryService::shelfStats() {
    return shelfDao_.listWithStats();
}

// ==================== 借阅业务（核心流程） ====================

/*
 * @brief 办理借书。
 * @param studentId 借书学生主键。
 * @param bookId 所借图书主键。
 * @return 新借阅记录主键。
 * @exception BusinessException 违反任一业务规则；NotFoundException 实体不存在；
 *           DbException 违反数据库约束（例如部分唯一索引拦截重复借出）。
 *
 * @note 主要算法（校验链 + 事务）：
 *   @code
 *   1 取学生 → 停用则拒绝                      (BR-01)
 *   2 hasOverdue(学生, 今天) → 逾期则拒绝      (BR-03)，直接比较 due_date，不依赖批量标记
 *   3 countActiveByStudent → 由领域对象 canBorrowMore 判定上限 (BR-01)
 *   4 取图书 → 非「在馆」则拒绝                (BR-02)
 *   5 构造 BorrowRecord：借出日 today()、应还日 addDays(today,30) (BR-04)
 *   6 checkEntity(record) 做日期/外键不变式校验
 *   7 BEGIN IMMEDIATE → INSERT 借阅记录 → UPDATE books.status=1 → COMMIT (BR-05/BR-12)
 *   @endcode
 *   第 7 步任一条 SQL 抛异常，栈展开触发 Transaction 析构自动 ROLLBACK，
 *   借阅记录与图书状态一起回滚，保证库存与单据永远一致。
 */
int LibraryService::borrowBook(int studentId, int bookId) {
    Student student = studentDao_.findById(studentId);       ///< 学生实体（含 maxBorrow / active）
    if (!student.active()) throw BusinessException("该学生已停用，不能借书");
    const std::string day = today();                         ///< 本次业务的统一基准日期
    if (loanDao_.hasOverdue(studentId, day))
        throw BusinessException("该生存在逾期未还图书，请先归还");
    const int active = loanDao_.countActiveByStudent(studentId);  ///< 当前已借册数
    if (!student.canBorrowMore(active))                      ///< 规则留在领域对象内
        throw BusinessException("已达可借上限 " +
                                std::to_string(student.maxBorrow()) + " 册");
    Book book = bookDao_.findById(bookId);                   ///< 图书实体（含状态）
    if (!book.isAvailable()) throw BusinessException("该图书当前不可借出");

    BorrowRecord record;                                     ///< 待插入的借阅单据
    record.setStudentId(studentId);                          ///< 学生外键
    record.setBookId(bookId);                                ///< 图书外键
    record.setBorrowDate(day);                               ///< 借出日 = 今天
    record.setDueDate(addDays(day, 30));                     ///< 应还日 = 今天 + 30 天
    record.setStatus(LoanStatus::Borrowed);                  ///< 初始状态：借出中
    record.setRemark("");                                    ///< 备注留空
    checkEntity(record);                                     ///< 多态校验日期与外键

    Transaction transaction(db_);                            ///< 开启事务（BEGIN IMMEDIATE）
    const int recordId = loanDao_.insert(record);            ///< 写借阅单据
    bookDao_.updateStatus(bookId, BookStatus::Borrowed);     ///< 同步更新库存状态
    transaction.commit();                                    ///< 提交；此前异常自动回滚
    return recordId;                                         ///< 返回新记录号
}

/*
 * @brief 办理还书。
 * @param recordId 借阅记录主键。
 * @exception BusinessException 记录已归还或状态已变更。
 * @note 主要算法：查询记录 → 拒绝重复归还 → 事务内
 *       「更新借阅记录为已归还 + 恢复图书在馆」→ 提交。
 *       与借书同样依赖 RAII 事务，异常时两步一起回滚。
 */
void LibraryService::returnBook(int recordId) {
    BorrowRecord record = loanDao_.findById(recordId);       ///< 取单据（需要 bookId）
    if (record.status() == LoanStatus::Returned)
        throw BusinessException("该记录已归还");
    const std::string day = today();                         ///< 归还日期
    Transaction transaction(db_);                            ///< 开启事务
    if (!loanDao_.close(recordId, day))
        throw BusinessException("归还失败：记录状态已变更");///< 抛异常 → 自动回滚
    bookDao_.updateStatus(record.bookId(), BookStatus::Available);  ///< 恢复在馆
    transaction.commit();                                    ///< 提交
}

/*
 * @brief 按筛选条件查询借阅明细。
 * @param filter 全部 / 当前借阅 / 逾期。
 * @return 借阅明细列表。
 */
std::vector<LoanDetail> LibraryService::listLoans(LoanFilter filter) {
    return loanDao_.listDetails(filter);
}

// ==================== 统计与维护 ====================

/*
 * @brief 汇总首页统计条的 6 项数字。
 * @return Statistics 结构体。
 * @note 主要算法：用**一条 SQL 的 6 个标量子查询**一次性取回全部计数，
 *       相比执行 6 条独立 COUNT 减少 5 次 SQLite 语句编译与往返；
 *       逾期数用 `status<>1 AND due_date < date('now','localtime')` 实时计算，
 *       因此即使尚未执行批量标记，统计条也准确。
 */
Statistics LibraryService::statistics() {
    Statistics result;                                       ///< 默认全 0 的统计结果
    Statement statement = db_.prepare(                        ///< 单条多子查询 SQL
        "SELECT "
        "(SELECT COUNT(*) FROM books),"
        "(SELECT COUNT(*) FROM books WHERE status=0),"
        "(SELECT COUNT(*) FROM books WHERE status=1),"
        "(SELECT COUNT(*) FROM borrow_records WHERE status<>1 "
        " AND due_date < date('now','localtime')),"
        "(SELECT COUNT(*) FROM students),"
        "(SELECT COUNT(*) FROM bookshelves);");
    if (statement.step()) {                                  ///< 该 SQL 恒返回一行
        result.totalBooks = statement.columnInt(0);          ///< 列 0：藏书总数
        result.availableBooks = statement.columnInt(1);      ///< 列 1：在馆数
        result.borrowedBooks = statement.columnInt(2);       ///< 列 2：借出数
        result.overdueLoans = statement.columnInt(3);        ///< 列 3：逾期数
        result.totalStudents = statement.columnInt(4);       ///< 列 4：学生数
        result.totalShelves = statement.columnInt(5);        ///< 列 5：书架数
    }
    return result;
}

/*
 * @brief 批量把到期未还记录标记为逾期。
 * @return 本次被标记的记录数，供状态栏提示。
 * @note 在启动时与每次「刷新全部」时调用；DAO 的 UPDATE 幂等，可安全重复执行。
 */
int LibraryService::refreshOverdue() {
    return loanDao_.markOverdue(today());
}

}  // namespace lls
