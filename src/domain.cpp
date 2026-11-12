/**
 * @file domain.cpp
 * @brief 领域模型层实现：枚举文本化、字符串与日期工具、四类实体的不变式校验与状态迁移。
 *
 * @section design 设计说明
 * - 所属层次：领域模型层（Domain），是四层架构中唯一不依赖 GUI、Service、DAO 与 SQLite 的一层，
 *   因此可被其它任意层复用，也便于单独做单元测试（tests/smoke_test.cpp 的 TC-01～TC-03）。
 * - 对应声明：include/domain.h。实体属性全部私有，本文件只实现头文件中声明的行为，
 *   不额外增加公开接口，保证「封装 + 单一职责」。
 * - 多态落点：validate() 与 toRow() 是虚函数，LibraryService::checkEntity(const Entity&) 与
 *   通用行导出只依赖基类引用，运行时动态绑定到具体实体实现。
 * - 异常策略：本层只抛出 BusinessException（状态迁移非法）；字段非法不抛异常，
 *   而是由 validate() 返回中文错误文本，交业务层统一转换为 ValidationException。
 * - 日期策略：全系统统一 `YYYY-MM-DD` 定长文本，便于 SQLite 的 date() 比较与字典序比较；
 *   本文件用 std::tm + mktime 完成日期加减的「溢出归一化」，避免自行实现闰年规则。
 *
 * @note 源文件编码 UTF-8 无 BOM；所有面向用户的文案均为简体中文。
 * @author 课程设计组
 * @date 2026-11-12
 */

#include "domain.h"

#include <cstdio>    // std::sscanf：解析定长日期字符串
#include <ctime>     // std::time / std::localtime / std::mktime / std::strftime
#include <sstream>   // 预留：与报告/日志模块保持一致的流式格式化能力

namespace lls {

/**
 * @brief 把图书状态枚举转换为界面显示的中文文本。
 * @param status 图书状态枚举值。
 * @return 常量字符串指针，生命周期与程序相同，无需释放。
 * @note 算法：穷举 switch；default 兜底返回「未知」，防止数据库脏值导致界面空白。
 */
const char* toString(BookStatus status) noexcept {
    switch (status) {
        case BookStatus::Available:   return "在馆";   ///< 可借出的馆藏副本
        case BookStatus::Borrowed:    return "借出";   ///< 存在未归还借阅记录
        case BookStatus::Lost:        return "遗失";   ///< 遗失，不可借
        case BookStatus::Maintenance: return "维修";   ///< 修补中，不可借
    }
    return "未知";                                    ///< 非法枚举值的兜底文案
}

/**
 * @brief 把借阅状态枚举转换为界面显示的中文文本。
 * @param status 借阅状态枚举值。
 * @return 常量字符串指针。
 * @note 与 borrow_records.status 的整数值 0/1/2 严格对应。
 */
const char* toString(LoanStatus status) noexcept {
    switch (status) {
        case LoanStatus::Borrowed: return "借出中";    ///< 尚未归还且未逾期
        case LoanStatus::Returned: return "已归还";    ///< 已完成归还，属历史记录
        case LoanStatus::Overdue:  return "逾期";      ///< 未归还且已超过应还日
    }
    return "未知";                                    ///< 非法枚举值兜底
}

/**
 * @brief 去除字符串首尾空白字符。
 * @param text 原始字符串（可能含空格、制表符、回车、换行）。
 * @return 去除首尾空白后的新字符串；全空白时返回空串。
 * @note 算法：先用 find_first_not_of 定位首个有效字符，再用 find_last_not_of 定位末个有效字符，
 *       最后 substr 一次截取；find 系列为 O(n)，整体 O(n)，无额外容器开销。
 */
std::string trim(const std::string& text) {
    const char* whitespace = " \t\r\n";                       ///< 视为空白的四个 ASCII 字符
    std::size_t begin = text.find_first_not_of(whitespace);   ///< 首个非空白字符下标
    if (begin == std::string::npos) return "";                ///< 全空白：直接返回空串
    std::size_t end = text.find_last_not_of(whitespace);      ///< 末个非空白字符下标
    return text.substr(begin, end - begin + 1);               ///< 闭区间 [begin, end] 的子串
}

/**
 * @brief 判断字符串是否为空或仅含空白。
 * @param text 待判定的字符串。
 * @return true 表示空白；false 表示含有效内容。
 * @note 复用 trim() 的结果，保证「Setter 清洗」与「validate 判定」口径完全一致。
 */
bool isBlank(const std::string& text) {
    return trim(text).empty();                                ///< trim 后为空即视为空白
}

namespace {  // 仅本编译单元可见的日期内部工具，不暴露到头文件（信息隐藏）

/**
 * @brief 解析 `YYYY-MM-DD` 文本并填充 std::tm。
 * @param value 待解析的日期字符串。
 * @param out 输出参数：解析成功时写入年/月/日，并置 tm_isdst = -1 让 mktime 自动判定夏令时。
 * @return 解析成功返回 true；长度、格式或数值非法返回 false。
 * @note 算法：长度必须恰为 10；用 sscanf 的 "%4d-%2d-%2d" 固定宽度拆分三段；
 *       再做粗粒度范围检查（年 >= 1900、月 1..12、日 1..31），精确日期合法性由 mktime 归一化处理。
 */
bool parseDate(const std::string& value, std::tm& out) {
    if (value.size() != 10) return false;                     ///< 定长格式的第一道校验
    int year = 0;                                             ///< 解析出的公历年份，如 2026
    int month = 0;                                            ///< 解析出的月份，范围 1..12
    int day = 0;                                              ///< 解析出的日，范围 1..31
    if (std::sscanf(value.c_str(), "%4d-%2d-%2d",
                    &year, &month, &day) != 3) return false;  ///< 未拆出三段即格式非法
    if (year < 1900 || month < 1 || month > 12 ||
        day < 1 || day > 31) return false;                    ///< 数值范围粗校验
    out = std::tm{};                                          ///< 清零，避免保留随机字段
    out.tm_year = year - 1900;                                ///< std::tm 年份以 1900 为基准
    out.tm_mon = month - 1;                                   ///< std::tm 月份以 0 表示一月
    out.tm_mday = day;                                        ///< 日无需偏移
    out.tm_isdst = -1;                                        ///< 交由 mktime 判断夏令时
    return true;
}

/**
 * @brief 把 std::tm 格式化为 `YYYY-MM-DD` 文本。
 * @param value 已归一化的日期结构。
 * @return 10 字符的日期字符串。
 * @note 使用 strftime 的 %Y-%m-%d，保证月/日补零，从而支持字典序比较。
 */
std::string formatDate(const std::tm& value) {
    char buffer[16] = {0};                                    ///< 输出缓冲，16 字节足以容纳 10 字符
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &value);  ///< 按定长格式写入
    return std::string(buffer);                               ///< 转为值语义的 std::string
}

}  // namespace 匿名命名空间结束

/**
 * @brief 取得本机当前日期。
 * @return `YYYY-MM-DD` 格式的今天日期。
 * @note 算法：time() 取 Unix 时间戳 → localtime 转本地时区结构 → formatDate 格式化。
 *       MSVC 使用线程安全版 localtime_s，MinGW/GCC 使用标准 localtime，
 *       通过 _MSC_VER 条件编译隔离平台差异。
 */
std::string today() {
    std::time_t stamp = std::time(nullptr);                   ///< 当前 Unix 时间戳（秒）
    std::tm local{};                                          ///< 本地时区的日历结构
#if defined(_MSC_VER)
    localtime_s(&local, &stamp);                              ///< MSVC 线程安全版本
#else
    local = *std::localtime(&stamp);                          ///< MinGW/GCC 标准版本
#endif
    return formatDate(local);                                 ///< 格式化为定长日期文本
}

/**
 * @brief 对日期做天数加减（支持负数）。
 * @param date 基准日期 `YYYY-MM-DD`。
 * @param days 增加的天数，可为负数（用于生成历史演示数据）。
 * @return 计算后的日期文本；输入非法或 mktime 失败时原样返回 date。
 * @note 主要算法（日期归一化）：
 *       1) parseDate 把文本拆成 std::tm；
 *       2) 直接对 tm_mday 做加法（允许出现 32 日、0 日、负数）；
 *       3) mktime 自动完成跨月、跨年、闰年与夏令时归一化；
 *       4) localtime 回读归一化结果并格式化。
 *       该做法把「闰年 2 月 29 日」「月末进位」等复杂规则交给标准库，避免自行实现出错。
 */
std::string addDays(const std::string& date, int days) {
    std::tm value{};                                          ///< 由输入文本解析出的日历结构
    if (!parseDate(date, value)) return date;                 ///< 输入非法：保持原值，不抛异常
    value.tm_mday += days;                                    ///< 允许越界，交给 mktime 归一化
    value.tm_isdst = -1;                                      ///< 重新判定夏令时
    std::time_t stamp = std::mktime(&value);                  ///< 归一化后的时间戳
    if (stamp == static_cast<std::time_t>(-1)) return date;   ///< 溢出/失败：保持原值
    std::tm normalized{};                                     ///< 归一化后的日历结构
#if defined(_MSC_VER)
    localtime_s(&normalized, &stamp);                         ///< MSVC 线程安全版本
#else
    normalized = *std::localtime(&stamp);                     ///< MinGW/GCC 标准版本
#endif
    return formatDate(normalized);                            ///< 输出定长日期文本
}

/**
 * @brief 判断日期 lhs 是否早于 rhs。
 * @param lhs 左侧定长日期文本。
 * @param rhs 右侧定长日期文本。
 * @return lhs < rhs 时返回 true。
 * @note 算法：因格式为固定宽度 `YYYY-MM-DD`（高位到低位、月日补零），
 *       字符串字典序与时间先后顺序完全等价，故直接比较字符串，无需再解析为时间戳。
 */
bool isBefore(const std::string& lhs, const std::string& rhs) {
    return lhs < rhs;                                         ///< 定长文本的字典序即时间序
}

/**
 * @brief 取得当前公历年份，用于实体中「出版年」的合法性上界。
 * @return 四位年份整数；异常时返回保守值 2100。
 * @note 算法：取 today() 的前 4 个字符转整数；try/catch 防止极端环境下 stoi 抛异常
 *       导致 validate() 中断业务写操作。
 */
int currentYear() {
    try {
        return std::stoi(today().substr(0, 4));               ///< 前四位即年份
    } catch (...) {
        return 2100;                                          ///< 兜底上界，永远不拦截合法新书
    }
}

// ==================== Book（图书实体） ====================

/**
 * @brief 校验图书实体的不变式。
 * @return 空串表示合法；否则返回第一条中文错误原因。
 * @note 校验顺序（fail-fast 算法）：ISBN 非空 → 题名非空 → 出版年非负 →
 *       出版年落在 [1450, 今年]（0 表示未填写，允许）→ 必须关联有效书架 ID。
 *       只返回首个错误，便于界面聚焦到第一个应修正的字段。
 */
std::string Book::validate() const {
    if (isBlank(isbn_)) return "ISBN 不能为空";               ///< 必填：国际标准书号
    if (isBlank(title_)) return "题名不能为空";               ///< 必填：图书题名
    if (publishYear_ < 0) return "出版年不能为负数";          ///< 负数无业务含义
    if (publishYear_ != 0 &&
        (publishYear_ < 1450 || publishYear_ > currentYear())) {
        return "出版年应在 1450 至今年之间";                  ///< 1450 约为活字印刷术起点
    }
    if (shelfId_ <= 0) return "必须选择所属书架";             ///< 外键预检：避免 SQL 层报错
    return "";                                                ///< 全部通过：空串代表合法
}

/**
 * @brief 多态行导出：把图书转为通用表格/日志所需的文本列。
 * @return 7 列文本：ID、ISBN、题名、作者、书架 ID、状态文本、出版年。
 * @note 该函数通过 Entity& 被通用入口调用，体现运行时多态；
 *       未关联书架时以 "-" 占位，避免界面显示 0 造成误解。
 */
std::vector<std::string> Book::toRow() const {
    return {std::to_string(id_),                             ///< 主键
            isbn_,                                           ///< ISBN
            title_,                                          ///< 题名
            author_,                                          ///< 作者
            shelfId_ > 0 ? std::to_string(shelfId_)
                         : std::string("-"),                 ///< 书架 ID 或占位符
            statusText(),                                    ///< 经枚举转换的中文状态
            std::to_string(publishYear_)};                   ///< 出版年（0 表示未填）
}

/**
 * @brief 状态迁移：在馆 → 借出。
 * @exception BusinessException 当前状态不是「在馆」时抛出。
 * @note 领域层只做状态合法性守卫；真正的持久化（更新 books.status 与插入借阅记录）
 *       由 LibraryService 在事务中完成，避免领域对象直接触碰数据库。
 */
void Book::markBorrowed() {
    if (!isAvailable()) throw BusinessException("该图书当前不可借出");  ///< 仅允许在馆状态借出
    status_ = BookStatus::Borrowed;                        ///< 迁移到「借出」
}

/** @brief 状态迁移：任意状态 → 在馆（归还或重新上架）。 */
void Book::markReturned() {
    status_ = BookStatus::Available;                       ///< 恢复可借状态
}

/**
 * @brief 把数据库中的整数状态安全转换为枚举。
 * @param value books.status 列的整数值。
 * @return 对应枚举；越界时回退为「在馆」，防止脏数据导致未定义行为。
 */
BookStatus Book::statusFromInt(int value) {
    if (value < 0 || value > 3) return BookStatus::Available; ///< 越界兜底
    return static_cast<BookStatus>(value);                    ///< 合法范围直接映射
}

// ==================== Student（学生实体） ====================

/**
 * @brief 校验学生实体的不变式。
 * @return 空串合法；否则首条错误原因。
 * @note 算法：学号、姓名必填；可借上限必须落在 [1, 20]，
 *       与 students.max_borrow 的 CHECK 约束保持一致（双重保险）。
 */
std::string Student::validate() const {
    if (isBlank(studentNo_)) return "学号不能为空";           ///< 必填且数据库 UNIQUE
    if (isBlank(name_)) return "姓名不能为空";                 ///< 必填
    if (maxBorrow_ < 1 || maxBorrow_ > 20) return "可借上限应在 1 至 20 之间";  ///< 业务上限
    return "";                                                ///< 合法
}

/**
 * @brief 多态行导出：学生 → 7 列文本。
 * @return ID、学号、姓名、专业、电话、可借上限、启用状态中文文本。
 */
std::vector<std::string> Student::toRow() const {
    return {std::to_string(id_),                             ///< 主键
            studentNo_,                                      ///< 学号
            name_,                                           ///< 姓名
            major_,                                          ///< 专业班级
            phone_,                                          ///< 联系电话
            std::to_string(maxBorrow_),                      ///< 可借上限
            active_ ? "启用" : "停用"};                      ///< 布尔状态转中文
}

/**
 * @brief 依据当前已借册数判断该学生是否还能借书（纯函数，不查库）。
 *        实现位于头文件：active_ && current < maxBorrow_。
 * @note 业务层在借书前先由 DAO 查询 current，再调用本方法，
 *       从而把「规则」留在领域对象内、把「数据获取」留在 DAO 内。
 */
// （canBorrowMore 为内联实现，见 domain.h；此处仅记录设计意图）

// ==================== Bookshelf（书架实体） ====================

/**
 * @brief 向内存聚合列表追加一本藏书 ID。
 * @param id 图书主键。
 * @exception BusinessException 书架已满时抛出。
 * @note 算法：remaining() = capacity_ - bookCount()；canAdd() 要求 remaining > 0。
 *       bookIds_ 是内存聚合，真实归属关系由 books.shelf_id 外键持久化，
 *       二者由 DAO 查询填充后保持一致。
 */
void Bookshelf::addBookId(int id) {
    if (!canAdd()) throw BusinessException("书架容量已满");   ///< 容量守卫
    bookIds_.push_back(id);                                   ///< 追加藏书 ID
}

/**
 * @brief 校验书架实体的不变式。
 * @return 空串合法；否则首条错误原因。
 */
std::string Bookshelf::validate() const {
    if (isBlank(code_)) return "书架编号不能为空";            ///< 必填且数据库 UNIQUE
    if (isBlank(location_)) return "书架位置不能为空";        ///< 必填：物理位置
    if (capacity_ <= 0) return "书架容量必须大于 0";          ///< 容量必须为正
    return "";                                                ///< 合法
}

/**
 * @brief 多态行导出：书架 → 6 列文本。
 * @return ID、编号、位置、分类、容量、当前藏书数。
 */
std::vector<std::string> Bookshelf::toRow() const {
    return {std::to_string(id_),                             ///< 主键
            code_,                                           ///< 书架编号
            location_,                                       ///< 所在位置
            category_,                                       ///< 分类
            std::to_string(capacity_),                       ///< 容量
            std::to_string(bookCount())};                    ///< 内存聚合的藏书数
}

// ==================== BorrowRecord（借阅记录实体） ====================

/**
 * @brief 判断该借阅记录是否逾期。
 * @param day 判定基准日（通常传 today()）。
 * @return 未归还且应还日早于 day 时返回 true。
 * @note 算法：status != Returned && dueDate < day；
 *       因日期为定长文本，直接复用 isBefore 做字典序比较，等价于时间比较。
 */
bool BorrowRecord::isOverdue(const std::string& day) const {
    return status_ != LoanStatus::Returned &&                ///< 已归还记录永远不算逾期
           isBefore(dueDate_, day);                          ///< 应还日早于今天
}

/**
 * @brief 关闭借阅记录（办理归还）。
 * @param day 归还日期 `YYYY-MM-DD`。
 * @exception BusinessException 重复归还时抛出。
 * @note 只修改内存对象；数据库层的 return_date / status 更新由 DAO 在事务中完成。
 */
void BorrowRecord::close(const std::string& day) {
    if (status_ == LoanStatus::Returned)
        throw BusinessException("该记录已归还");              ///< 幂等保护：拒绝重复归还
    returnDate_ = day;                                       ///< 登记归还日期
    status_ = LoanStatus::Returned;                          ///< 状态迁移为已归还
}

/**
 * @brief 把数据库中的整数状态安全转换为借阅枚举。
 * @param value borrow_records.status 的整数值。
 * @return 对应枚举；越界回退为「借出中」。
 */
LoanStatus BorrowRecord::statusFromInt(int value) {
    if (value < 0 || value > 2) return LoanStatus::Borrowed; ///< 越界兜底
    return static_cast<LoanStatus>(value);                    ///< 合法范围直接映射
}

/**
 * @brief 校验借阅记录的不变式。
 * @return 空串合法；否则首条错误原因。
 * @note 算法：外键 ID 必须为正 → 借出日与应还日必须是合法定长日期 →
 *       应还日不得早于借出日 → 若已填写归还日，则格式合法且不得早于借出日。
 *       「归还日晚于应还日」是允许的，用于演示逾期归还。
 */
std::string BorrowRecord::validate() const {
    if (studentId_ <= 0) return "学生 ID 无效";              ///< 外键预检
    if (bookId_ <= 0) return "图书 ID 无效";                  ///< 外键预检
    std::tm ignored{};                                       ///< parseDate 的输出占位，本函数不关心具体值
    if (!parseDate(borrowDate_, ignored)) return "借出日期格式应为 YYYY-MM-DD";
    if (!parseDate(dueDate_, ignored)) return "应还日期格式应为 YYYY-MM-DD";
    if (isBefore(dueDate_, borrowDate_)) return "应还日期不能早于借出日期";
    if (!returnDate_.empty()) {                              ///< 归还日可为空（尚未归还）
        if (!parseDate(returnDate_, ignored)) return "归还日期格式应为 YYYY-MM-DD";
        if (isBefore(returnDate_, borrowDate_)) return "归还日期不能早于借出日期";
    }
    return "";                                               ///< 合法
}

/**
 * @brief 多态行导出：借阅记录 → 8 列文本。
 * @return ID、学生 ID、图书 ID、借出日、应还日、归还日、状态文本、备注。
 */
std::vector<std::string> BorrowRecord::toRow() const {
    return {std::to_string(id_),                             ///< 记录主键
            std::to_string(studentId_),                      ///< 学生外键
            std::to_string(bookId_),                         ///< 图书外键
            borrowDate_,                                     ///< 借出日期
            dueDate_,                                        ///< 应还日期
            returnDate_.empty() ? std::string("-")
                                : returnDate_,               ///< 未归还时以 "-" 占位
            statusText(),                                    ///< 中文状态
            remark_};                                        ///< 备注
}

}  // namespace lls
