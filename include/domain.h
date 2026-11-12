/**
 * @file domain.h
 * @brief 领域模型层声明：枚举、工具函数、异常体系、Entity 抽象基类与四类业务实体。
 *
 * @par 设计说明
 * - 所属层次：领域模型层（Domain），四层架构的最内层；**不依赖** GUI、Service、DAO 或 SQLite，
 *   只使用 C++ 标准库，因此可被任意上层复用并可单独做单元测试。
 * - 实现位置：`src/domain.cpp`（非内联函数的实现）；本头文件只放声明与短小内联函数。
 * - 面向对象设计点：
 *   1) 封装：全部属性私有，setter 对字符串统一执行 `trim()` 清洗，getter 返回 `const&` 或值；
 *   2) 继承：`Book / Student / Bookshelf / BorrowRecord` 公有继承 `Entity`；异常族继承 `AppException`；
 *   3) 多态：`validate()` 与 `toRow()` 为纯虚函数，业务层通过 `const Entity&` 动态调用；
 *   4) 值语义：实体可拷贝、可按值放入 `std::vector`，DAO 以此返回查询结果。
 * - 不变式约定：字段合法性由 `validate()` 返回中文错误文案（不抛异常），
 *   由 `LibraryService::checkEntity()` 统一转成 `ValidationException`；
 *   状态迁移非法时（如重复归还）才由实体方法直接抛 `BusinessException`。
 * - 编码与日期：所有文本 UTF-8；日期统一 `YYYY-MM-DD` 定长字符串，可直接字典序比较。
 *
 * @author 课程设计组
 * @date 2026-11-12
 */

#pragma once

#include <stdexcept>  // std::runtime_error：异常体系基类
#include <string>     // std::string：全部文本字段
#include <vector>     // std::vector：多态行导出与书架藏书 ID 聚合

namespace lls {

// ==================== 枚举：与数据库整数列严格对应 ====================

/** @brief 图书状态；整数值对应 books.status 列的 0～3。 */
enum class BookStatus {
    Available = 0,    ///< 在馆，可借出
    Borrowed = 1,     ///< 已借出，存在未归还记录
    Lost = 2,         ///< 遗失，不可借
    Maintenance = 3   ///< 维修中，不可借
};

/** @brief 借阅记录状态；整数值对应 borrow_records.status 列的 0～2。 */
enum class LoanStatus {
    Borrowed = 0,     ///< 借出中（未归还、未逾期）
    Returned = 1,     ///< 已归还，属历史记录
    Overdue = 2       ///< 逾期未还
};

/** @brief 借阅页筛选条件；界面 Fl_Choice 的选项序号与本枚举值一一对应。 */
enum class LoanFilter {
    All = 0,          ///< 全部记录（含历史）
    Active = 1,       ///< 当前未归还（status 0 或 2）
    Overdue = 2       ///< 逾期记录
};

/**
 * @brief 把图书状态枚举转换为界面中文文本。
 * @param status 图书状态。
 * @return 常量字符串（在馆 / 借出 / 遗失 / 维修 / 未知）。
 */
const char* toString(BookStatus status) noexcept;

/**
 * @brief 把借阅状态枚举转换为界面中文文本。
 * @param status 借阅状态。
 * @return 常量字符串（借出中 / 已归还 / 逾期 / 未知）。
 */
const char* toString(LoanStatus status) noexcept;

// ==================== 值工具函数 ====================

/**
 * @brief 去除字符串首尾空白。
 * @param text 原始文本。
 * @return 清洗后的新字符串；全空白返回空串。
 * @note 实现见 domain.cpp：find_first_not_of + find_last_not_of 一次截取，O(n)。
 */
std::string trim(const std::string& text);

/**
 * @brief 判定字符串是否为空或仅含空白。
 * @param text 待判定文本。
 * @return true 表示空白。
 */
bool isBlank(const std::string& text);

/**
 * @brief 取得本机今天的日期。
 * @return `YYYY-MM-DD` 文本。
 * @note 实现隔离 MSVC 的 localtime_s 与 MinGW 的 std::localtime。
 */
std::string today();

/**
 * @brief 日期加减（支持负数）。
 * @param date 基准日期文本。
 * @param days 增加的天数。
 * @return 归一化后的日期文本；输入非法时原样返回。
 * @note 算法：解析为 std::tm → 直接对 tm_mday 做加法 → mktime 自动处理跨月、跨年与闰年。
 */
std::string addDays(const std::string& date, int days);

/**
 * @brief 判断 lhs 日期是否早于 rhs。
 * @param lhs 左侧定长日期。
 * @param rhs 右侧定长日期。
 * @return lhs < rhs 时返回 true。
 * @note 定长 `YYYY-MM-DD` 的字典序与时间先后等价，无需解析时间戳。
 */
bool isBefore(const std::string& lhs, const std::string& rhs);

/**
 * @brief 取得当前公历年份，作为实体「出版年」的合法上界。
 * @return 四位年份；异常时返回 2100。
 */
int currentYear();

// ==================== 异常体系 ====================

/**
 * @brief 所有应用异常的公共基类，继承 std::runtime_error。
 * @note 界面层只需捕获 `const AppException&` 即可统一处理业务/校验/数据库错误。
 */
class AppException : public std::runtime_error {
public:
    /** @brief 构造异常。 @param message 面向用户的中文错误文案。 */
    explicit AppException(const std::string& message) : std::runtime_error(message) {}
};

/** @brief 输入或实体不变式非法时抛出（如题名为空、可借上限越界）。 */
class ValidationException : public AppException {
public:
    /** @brief 构造异常。 @param m 错误文案。 */
    explicit ValidationException(const std::string& m) : AppException(m) {}
};

/** @brief 按主键查询无结果时抛出（DAO 的 findById 统一行为）。 */
class NotFoundException : public AppException {
public:
    /** @brief 构造异常。 @param m 错误文案。 */
    explicit NotFoundException(const std::string& m) : AppException(m) {}
};

/** @brief SQLite 执行或预编译失败时抛出，消息中附带原始 SQL 与 errmsg。 */
class DbException : public AppException {
public:
    /** @brief 构造异常。 @param m 错误文案。 */
    explicit DbException(const std::string& m) : AppException(m) {}
};

/** @brief 违反业务规则时抛出（停用/逾期/超上限/重复借出/删除被引用等）。 */
class BusinessException : public AppException {
public:
    /** @brief 构造异常。 @param m 面向用户的业务文案。 */
    explicit BusinessException(const std::string& m) : AppException(m) {}
};

// ==================== Entity 抽象基类 ====================

/**
 * @brief 全部业务实体的抽象基类，定义主键与三个多态契约。
 * @note 设计要点：`LibraryService::checkEntity(const Entity&)` 只依赖基类引用调用
 *       `validate()`；`toRow()` 支持把任意实体导出为通用表格行，二者均是运行时多态落点。
 */
class Entity {
protected:
    int id_ = 0;    ///< 数据库自增主键；0 表示尚未持久化

public:
    /** @brief 虚析构，保证通过基类指针删除派生对象时正确析构。 */
    virtual ~Entity() = default;

    /** @brief 取主键。 @return 主键值，0 表示未持久化。 */
    int id() const noexcept { return id_; }

    /** @brief 设置主键（DAO 从数据库读出行后回填）。 @param value 主键值。 */
    void setId(int value) noexcept { id_ = value; }

    /** @brief 返回对应数据表名。 @return 表名（如 "books"），用于日志与诊断。 */
    virtual std::string tableName() const = 0;

    /** @brief 校验实体不变式。 @return 空串表示合法，否则返回首条中文错误原因。 */
    virtual std::string validate() const = 0;

    /** @brief 多态行导出。 @return 各列文本，供通用表格/日志使用。 */
    virtual std::vector<std::string> toRow() const = 0;
};

// ==================== Book：馆藏副本 ====================

/**
 * @brief 图书实体；一条记录代表一册可借的实体书，因此 ISBN 允许重复。
 * @note 状态迁移 `markBorrowed()/markReturned()` 只改内存对象，
 *       数据库更新由 `LibraryService` 在事务中完成，领域层不触碰 SQL。
 */
class Book : public Entity {
    std::string isbn_;        ///< 国际标准书号（必填，允许重复）
    std::string title_;       ///< 题名（必填）
    std::string author_;      ///< 作者（可空）
    std::string publisher_;   ///< 出版社（可空）
    std::string category_;    ///< 分类（可空）
    std::string addedAt_;     ///< 入库日期 `YYYY-MM-DD`
    int publishYear_ = 0;     ///< 出版年；0 表示未填写
    int shelfId_ = 0;         ///< 所属书架外键（必须大于 0）
    BookStatus status_ = BookStatus::Available;  ///< 当前状态，默认在馆

public:
    /** @brief 默认构造：id=0、状态=在馆、其余字段为空。 */
    Book() = default;

    /** @brief 取 ISBN。 @return ISBN 常量引用。 */
    const std::string& isbn() const noexcept { return isbn_; }
    /** @brief 取题名。 @return 题名常量引用。 */
    const std::string& title() const noexcept { return title_; }
    /** @brief 取作者。 @return 作者常量引用。 */
    const std::string& author() const noexcept { return author_; }
    /** @brief 取出版社。 @return 出版社常量引用。 */
    const std::string& publisher() const noexcept { return publisher_; }
    /** @brief 取分类。 @return 分类常量引用。 */
    const std::string& category() const noexcept { return category_; }
    /** @brief 取入库日期。 @return 日期文本常量引用。 */
    const std::string& addedAt() const noexcept { return addedAt_; }
    /** @brief 取出版年。 @return 年份，0 表示未填。 */
    int publishYear() const noexcept { return publishYear_; }
    /** @brief 取书架外键。 @return 书架 ID。 */
    int shelfId() const noexcept { return shelfId_; }
    /** @brief 取图书状态。 @return 状态枚举。 */
    BookStatus status() const noexcept { return status_; }

    /** @brief 设置 ISBN（自动 trim）。 @param v 新值。 */
    void setIsbn(const std::string& v) { isbn_ = trim(v); }
    /** @brief 设置题名（自动 trim）。 @param v 新值。 */
    void setTitle(const std::string& v) { title_ = trim(v); }
    /** @brief 设置作者（自动 trim）。 @param v 新值。 */
    void setAuthor(const std::string& v) { author_ = trim(v); }
    /** @brief 设置出版社（自动 trim）。 @param v 新值。 */
    void setPublisher(const std::string& v) { publisher_ = trim(v); }
    /** @brief 设置分类（自动 trim）。 @param v 新值。 */
    void setCategory(const std::string& v) { category_ = trim(v); }
    /** @brief 设置入库日期（自动 trim）。 @param v 新值。 */
    void setAddedAt(const std::string& v) { addedAt_ = trim(v); }
    /** @brief 设置出版年。 @param v 年份，0 表示未填。 */
    void setPublishYear(int v) noexcept { publishYear_ = v; }
    /** @brief 设置书架外键。 @param v 书架 ID。 */
    void setShelfId(int v) noexcept { shelfId_ = v; }
    /** @brief 设置状态枚举。 @param v 新状态。 */
    void setStatus(BookStatus v) noexcept { status_ = v; }

    /** @brief 表名多态实现。 @return "books"。 */
    std::string tableName() const override { return "books"; }
    /** @brief 校验 ISBN/题名/出版年/书架外键。 @return 首条错误文案或空串。 */
    std::string validate() const override;
    /** @brief 导出 7 列文本。 @return ID、ISBN、题名、作者、书架、状态、出版年。 */
    std::vector<std::string> toRow() const override;

    /** @brief 是否可借出。 @return 状态为「在馆」时返回 true。 */
    bool isAvailable() const noexcept { return status_ == BookStatus::Available; }
    /** @brief 取状态中文文本。 @return 如「在馆」「借出」。 */
    const char* statusText() const noexcept { return toString(status_); }

    /** @brief 状态迁移：在馆 → 借出。 @exception BusinessException 当前不可借时抛出。 */
    void markBorrowed();
    /** @brief 状态迁移：任意状态 → 在馆（归还或重新上架）。 */
    void markReturned();
    /**
     * @brief 把数据库整数状态安全转换为枚举。
     * @param value books.status 的整数值。
     * @return 对应枚举；越界回退为「在馆」，防止脏数据导致未定义行为。
     */
    static BookStatus statusFromInt(int value);
};

// ==================== Student：读者 ====================

/**
 * @brief 学生（读者）实体。
 * @note 「可借上限」规则封装在 `canBorrowMore()` 中，业务层只负责从 DAO 取当前已借数，
 *       从而实现「规则在领域层、取数在 DAO 层」的职责分离。
 */
class Student : public Entity {
    std::string studentNo_;   ///< 学号（必填，数据库 UNIQUE）
    std::string name_;        ///< 姓名（必填）
    std::string major_;       ///< 专业班级（可空）
    std::string phone_;       ///< 联系电话（可空）
    int maxBorrow_ = 5;       ///< 可借上限，合法范围 1～20
    bool active_ = true;      ///< 是否启用；停用学生禁止借书

public:
    /** @brief 默认构造：上限 5、启用、其余为空。 */
    Student() = default;

    /** @brief 取学号。 @return 学号常量引用。 */
    const std::string& studentNo() const noexcept { return studentNo_; }
    /** @brief 取姓名。 @return 姓名常量引用。 */
    const std::string& name() const noexcept { return name_; }
    /** @brief 取专业班级。 @return 专业常量引用。 */
    const std::string& major() const noexcept { return major_; }
    /** @brief 取联系电话。 @return 电话常量引用。 */
    const std::string& phone() const noexcept { return phone_; }
    /** @brief 取可借上限。 @return 最大同时借阅册数。 */
    int maxBorrow() const noexcept { return maxBorrow_; }
    /** @brief 是否启用。 @return true 表示允许借书。 */
    bool active() const noexcept { return active_; }

    /** @brief 设置学号（自动 trim）。 @param v 新值。 */
    void setStudentNo(const std::string& v) { studentNo_ = trim(v); }
    /** @brief 设置姓名（自动 trim）。 @param v 新值。 */
    void setName(const std::string& v) { name_ = trim(v); }
    /** @brief 设置专业班级（自动 trim）。 @param v 新值。 */
    void setMajor(const std::string& v) { major_ = trim(v); }
    /** @brief 设置联系电话（自动 trim）。 @param v 新值。 */
    void setPhone(const std::string& v) { phone_ = trim(v); }
    /** @brief 设置可借上限。 @param v 1～20 之间的整数。 */
    void setMaxBorrow(int v) noexcept { maxBorrow_ = v; }
    /** @brief 设置启用状态。 @param v true 启用，false 停用。 */
    void setActive(bool v) noexcept { active_ = v; }

    /**
     * @brief 判断在已借 current 册的情况下是否还能再借。
     * @param current 当前未归还册数（由 DAO 查询得到）。
     * @return 启用且未达上限时返回 true。
     * @note 算法：`active_ && current < maxBorrow_`；停用学生一票否决。
     */
    bool canBorrowMore(int current) const noexcept {
        return active_ && current < maxBorrow_;
    }
    /** @brief 取系统默认可借上限。 @return 5。 */
    static int defaultMaxBorrow() noexcept { return 5; }

    /** @brief 表名多态实现。 @return "students"。 */
    std::string tableName() const override { return "students"; }
    /** @brief 校验学号、姓名与可借上限范围。 @return 首条错误文案或空串。 */
    std::string validate() const override;
    /** @brief 导出 7 列文本。 @return ID、学号、姓名、专业、电话、上限、状态。 */
    std::vector<std::string> toRow() const override;
};

// ==================== Bookshelf：书架（内存聚合藏书 ID） ====================

/**
 * @brief 书架实体。
 * @note 聚合关系：`bookIds_` 是内存中的藏书 ID 列表（展示用），
 *       真实归属由 `books.shelf_id` 外键持久化；二者由 DAO 查询填充后保持一致。
 */
class Bookshelf : public Entity {
    std::string code_;        ///< 书架编号（必填，数据库 UNIQUE）
    std::string location_;    ///< 物理位置（必填）
    std::string category_;    ///< 分类（可空）
    std::string remark_;      ///< 备注（可空）
    int capacity_ = 0;        ///< 容量（册），必须大于 0
    std::vector<int> bookIds_;///< 内存聚合的藏书 ID 列表，不直接持久化

public:
    /** @brief 默认构造：容量 0、其余为空。 */
    Bookshelf() = default;

    /** @brief 取书架编号。 @return 编号常量引用。 */
    const std::string& code() const noexcept { return code_; }
    /** @brief 取所在位置。 @return 位置常量引用。 */
    const std::string& location() const noexcept { return location_; }
    /** @brief 取分类。 @return 分类常量引用。 */
    const std::string& category() const noexcept { return category_; }
    /** @brief 取备注。 @return 备注常量引用。 */
    const std::string& remark() const noexcept { return remark_; }
    /** @brief 取容量。 @return 可容纳册数。 */
    int capacity() const noexcept { return capacity_; }
    /** @brief 取内存聚合的藏书 ID 列表。 @return 只读引用。 */
    const std::vector<int>& bookIds() const noexcept { return bookIds_; }

    /** @brief 设置编号（自动 trim）。 @param v 新值。 */
    void setCode(const std::string& v) { code_ = trim(v); }
    /** @brief 设置位置（自动 trim）。 @param v 新值。 */
    void setLocation(const std::string& v) { location_ = trim(v); }
    /** @brief 设置分类（自动 trim）。 @param v 新值。 */
    void setCategory(const std::string& v) { category_ = trim(v); }
    /** @brief 设置备注（自动 trim）。 @param v 新值。 */
    void setRemark(const std::string& v) { remark_ = trim(v); }
    /** @brief 设置容量。 @param v 正整数。 */
    void setCapacity(int v) noexcept { capacity_ = v; }

    /** @brief 当前藏书数。 @return bookIds_ 的元素个数。 */
    std::size_t bookCount() const noexcept { return bookIds_.size(); }
    /** @brief 剩余可用册位。 @return 容量减已藏数，可能为负（容量被调小时）。 */
    int remaining() const noexcept {
        return capacity_ - static_cast<int>(bookIds_.size());
    }
    /** @brief 是否还能放书。 @return remaining() > 0 时返回 true。 */
    bool canAdd() const noexcept { return remaining() > 0; }
    /**
     * @brief 追加一本藏书 ID。
     * @param id 图书主键。
     * @exception BusinessException 书架已满时抛出。
     */
    void addBookId(int id);
    /** @brief 清空内存聚合的藏书 ID 列表（不影响数据库）。 */
    void clearBookIds() noexcept { bookIds_.clear(); }

    /** @brief 表名多态实现。 @return "bookshelves"。 */
    std::string tableName() const override { return "bookshelves"; }
    /** @brief 校验编号、位置与容量。 @return 首条错误文案或空串。 */
    std::string validate() const override;
    /** @brief 导出 6 列文本。 @return ID、编号、位置、分类、容量、藏书数。 */
    std::vector<std::string> toRow() const override;
};

// ==================== BorrowRecord：借阅单据 ====================

/**
 * @brief 借阅记录实体；一次借书行为产生一条单据，历史记录永久保留。
 * @note 同一册书同时最多一条未归还记录，由数据库部分唯一索引 `uq_active_loan`
 *       与业务层校验共同保证；`returnDate_` 为空串对应 SQL NULL。
 */
class BorrowRecord : public Entity {
    int studentId_ = 0;       ///< 学生外键（必须大于 0）
    int bookId_ = 0;          ///< 图书外键（必须大于 0）
    std::string borrowDate_;  ///< 借出日期 `YYYY-MM-DD`
    std::string dueDate_;     ///< 应还日期，通常为借出日 + 30 天
    std::string returnDate_;  ///< 归还日期；空串表示尚未归还
    std::string remark_;      ///< 备注（可空）
    LoanStatus status_ = LoanStatus::Borrowed;  ///< 当前状态，默认借出中

public:
    /** @brief 默认构造：外键为 0、状态为借出中、日期为空。 */
    BorrowRecord() = default;

    /** @brief 取学生外键。 @return 学生 ID。 */
    int studentId() const noexcept { return studentId_; }
    /** @brief 取图书外键。 @return 图书 ID。 */
    int bookId() const noexcept { return bookId_; }
    /** @brief 取借出日期。 @return 日期文本常量引用。 */
    const std::string& borrowDate() const noexcept { return borrowDate_; }
    /** @brief 取应还日期。 @return 日期文本常量引用。 */
    const std::string& dueDate() const noexcept { return dueDate_; }
    /** @brief 取归还日期。 @return 日期文本；空串表示未归还。 */
    const std::string& returnDate() const noexcept { return returnDate_; }
    /** @brief 取备注。 @return 备注常量引用。 */
    const std::string& remark() const noexcept { return remark_; }
    /** @brief 取借阅状态。 @return 状态枚举。 */
    LoanStatus status() const noexcept { return status_; }

    /** @brief 设置学生外键。 @param v 学生 ID。 */
    void setStudentId(int v) noexcept { studentId_ = v; }
    /** @brief 设置图书外键。 @param v 图书 ID。 */
    void setBookId(int v) noexcept { bookId_ = v; }
    /** @brief 设置借出日期（自动 trim）。 @param v 日期文本。 */
    void setBorrowDate(const std::string& v) { borrowDate_ = trim(v); }
    /** @brief 设置应还日期（自动 trim）。 @param v 日期文本。 */
    void setDueDate(const std::string& v) { dueDate_ = trim(v); }
    /** @brief 设置归还日期（自动 trim）。 @param v 日期文本，空串表示未归还。 */
    void setReturnDate(const std::string& v) { returnDate_ = trim(v); }
    /** @brief 设置备注（自动 trim）。 @param v 新值。 */
    void setRemark(const std::string& v) { remark_ = trim(v); }
    /** @brief 设置状态枚举。 @param v 新状态。 */
    void setStatus(LoanStatus v) noexcept { status_ = v; }

    /**
     * @brief 判断是否逾期。
     * @param day 判定基准日（通常传 today()）。
     * @return 未归还且应还日早于 day 时返回 true。
     * @note 算法：状态非「已归还」且 `isBefore(dueDate_, day)`；
     *       定长日期的字典序即时间序，无需解析时间戳。
     */
    bool isOverdue(const std::string& day) const;
    /**
     * @brief 关闭单据（办理归还）。
     * @param day 归还日期。
     * @exception BusinessException 重复归还时抛出（幂等保护）。
     */
    void close(const std::string& day);
    /** @brief 取状态中文文本。 @return 如「借出中」「已归还」「逾期」。 */
    const char* statusText() const noexcept { return toString(status_); }
    /**
     * @brief 把数据库整数状态安全转换为枚举。
     * @param value borrow_records.status 的整数值。
     * @return 对应枚举；越界回退为「借出中」。
     */
    static LoanStatus statusFromInt(int value);

    /** @brief 表名多态实现。 @return "borrow_records"。 */
    std::string tableName() const override { return "borrow_records"; }
    /** @brief 校验外键、日期格式与先后关系。 @return 首条错误文案或空串。 */
    std::string validate() const override;
    /** @brief 导出 8 列文本。 @return ID、学生、图书、借出、应还、归还、状态、备注。 */
    std::vector<std::string> toRow() const override;
};

}  // namespace lls
