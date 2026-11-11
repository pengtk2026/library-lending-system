// domain.cpp -- implementation of domain entities and helpers
#include "domain.h"

#include <cstdio>
#include <ctime>
#include <sstream>

namespace lls {

const char* toString(BookStatus status) noexcept {
    switch (status) {
        case BookStatus::Available:   return "在馆";
        case BookStatus::Borrowed:    return "借出";
        case BookStatus::Lost:        return "遗失";
        case BookStatus::Maintenance: return "维修";
    }
    return "未知";
}

const char* toString(LoanStatus status) noexcept {
    switch (status) {
        case LoanStatus::Borrowed: return "借出中";
        case LoanStatus::Returned: return "已归还";
        case LoanStatus::Overdue:  return "逾期";
    }
    return "未知";
}

std::string trim(const std::string& text) {
    const char* whitespace = " \t\r\n";
    std::size_t begin = text.find_first_not_of(whitespace);
    if (begin == std::string::npos) return "";
    std::size_t end = text.find_last_not_of(whitespace);
    return text.substr(begin, end - begin + 1);
}

bool isBlank(const std::string& text) {
    return trim(text).empty();
}

namespace {
bool parseDate(const std::string& value, std::tm& out) {
    if (value.size() != 10) return false;
    int year = 0, month = 0, day = 0;
    if (std::sscanf(value.c_str(), "%4d-%2d-%2d", &year, &month, &day) != 3) return false;
    if (year < 1900 || month < 1 || month > 12 || day < 1 || day > 31) return false;
    out = std::tm{};
    out.tm_year = year - 1900;
    out.tm_mon = month - 1;
    out.tm_mday = day;
    out.tm_isdst = -1;
    return true;
}

std::string formatDate(const std::tm& value) {
    char buffer[16] = {0};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &value);
    return std::string(buffer);
}
}  // namespace

std::string today() {
    std::time_t stamp = std::time(nullptr);
    std::tm local{};
#if defined(_MSC_VER)
    localtime_s(&local, &stamp);
#else
    local = *std::localtime(&stamp);
#endif
    return formatDate(local);
}

std::string addDays(const std::string& date, int days) {
    std::tm value{};
    if (!parseDate(date, value)) return date;
    value.tm_mday += days;
    value.tm_isdst = -1;
    std::time_t stamp = std::mktime(&value);
    if (stamp == static_cast<std::time_t>(-1)) return date;
    std::tm normalized{};
#if defined(_MSC_VER)
    localtime_s(&normalized, &stamp);
#else
    normalized = *std::localtime(&stamp);
#endif
    return formatDate(normalized);
}

bool isBefore(const std::string& lhs, const std::string& rhs) {
    return lhs < rhs;  // fixed-width YYYY-MM-DD strings compare lexicographically
}

int currentYear() {
    try {
        return std::stoi(today().substr(0, 4));
    } catch (...) {
        return 2100;
    }
}

// ---------- Book ----------
std::string Book::validate() const {
    if (isBlank(isbn_)) return "ISBN 不能为空";
    if (isBlank(title_)) return "题名不能为空";
    if (publishYear_ < 0) return "出版年不能为负数";
    if (publishYear_ != 0 &&
        (publishYear_ < 1450 || publishYear_ > currentYear())) {
        return "出版年应在 1450 至今年之间";
    }
    if (shelfId_ <= 0) return "必须选择所属书架";
    return "";
}

std::vector<std::string> Book::toRow() const {
    return {std::to_string(id_), isbn_, title_, author_, shelfId_ > 0
            ? std::to_string(shelfId_) : std::string("-"),
            statusText(), std::to_string(publishYear_)};
}

void Book::markBorrowed() {
    if (!isAvailable()) throw BusinessException("该图书当前不可借出");
    status_ = BookStatus::Borrowed;
}

void Book::markReturned() {
    status_ = BookStatus::Available;
}

BookStatus Book::statusFromInt(int value) {
    if (value < 0 || value > 3) return BookStatus::Available;
    return static_cast<BookStatus>(value);
}

// ---------- Student ----------
std::string Student::validate() const {
    if (isBlank(studentNo_)) return "学号不能为空";
    if (isBlank(name_)) return "姓名不能为空";
    if (maxBorrow_ < 1 || maxBorrow_ > 20) return "可借上限应在 1 至 20 之间";
    return "";
}

std::vector<std::string> Student::toRow() const {
    return {std::to_string(id_), studentNo_, name_, major_, phone_,
            std::to_string(maxBorrow_), active_ ? "启用" : "停用"};
}

// ---------- Bookshelf ----------
void Bookshelf::addBookId(int id) {
    if (!canAdd()) throw BusinessException("书架容量已满");
    bookIds_.push_back(id);
}

std::string Bookshelf::validate() const {
    if (isBlank(code_)) return "书架编号不能为空";
    if (isBlank(location_)) return "书架位置不能为空";
    if (capacity_ <= 0) return "书架容量必须大于 0";
    return "";
}

std::vector<std::string> Bookshelf::toRow() const {
    return {std::to_string(id_), code_, location_, category_,
            std::to_string(capacity_), std::to_string(bookCount())};
}

// ---------- BorrowRecord ----------
bool BorrowRecord::isOverdue(const std::string& day) const {
    return status_ != LoanStatus::Returned && isBefore(dueDate_, day);
}

void BorrowRecord::close(const std::string& day) {
    if (status_ == LoanStatus::Returned) throw BusinessException("该记录已归还");
    returnDate_ = day;
    status_ = LoanStatus::Returned;
}

LoanStatus BorrowRecord::statusFromInt(int value) {
    if (value < 0 || value > 2) return LoanStatus::Borrowed;
    return static_cast<LoanStatus>(value);
}

std::string BorrowRecord::validate() const {
    if (studentId_ <= 0) return "学生 ID 无效";
    if (bookId_ <= 0) return "图书 ID 无效";
    std::tm ignored{};
    if (!parseDate(borrowDate_, ignored)) return "借出日期格式应为 YYYY-MM-DD";
    if (!parseDate(dueDate_, ignored)) return "应还日期格式应为 YYYY-MM-DD";
    if (isBefore(dueDate_, borrowDate_)) return "应还日期不能早于借出日期";
    if (!returnDate_.empty()) {
        if (!parseDate(returnDate_, ignored)) return "归还日期格式应为 YYYY-MM-DD";
        if (isBefore(returnDate_, borrowDate_)) return "归还日期不能早于借出日期";
    }
    return "";
}

std::vector<std::string> BorrowRecord::toRow() const {
    return {std::to_string(id_), std::to_string(studentId_),
            std::to_string(bookId_), borrowDate_, dueDate_,
            returnDate_.empty() ? std::string("-") : returnDate_,
            statusText(), remark_};
}

}  // namespace lls
