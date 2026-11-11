#pragma once
// domain.h -- domain model layer: enumerations, helpers, exceptions, entities
#include <stdexcept>
#include <string>
#include <vector>

namespace lls {

// ---------- enumerations ----------
enum class BookStatus { Available = 0, Borrowed = 1, Lost = 2, Maintenance = 3 };
enum class LoanStatus { Borrowed = 0, Returned = 1, Overdue = 2 };
enum class LoanFilter { All = 0, Active = 1, Overdue = 2 };

const char* toString(BookStatus status) noexcept;
const char* toString(LoanStatus status) noexcept;

// ---------- value helpers ----------
std::string trim(const std::string& text);
bool isBlank(const std::string& text);
std::string today();
std::string addDays(const std::string& date, int days);
bool isBefore(const std::string& lhs, const std::string& rhs);
int currentYear();

// ---------- exception hierarchy ----------
class AppException : public std::runtime_error {
public:
    explicit AppException(const std::string& message) : std::runtime_error(message) {}
};
class ValidationException : public AppException {
public:
    explicit ValidationException(const std::string& m) : AppException(m) {}
};
class NotFoundException : public AppException {
public:
    explicit NotFoundException(const std::string& m) : AppException(m) {}
};
class DbException : public AppException {
public:
    explicit DbException(const std::string& m) : AppException(m) {}
};
class BusinessException : public AppException {
public:
    explicit BusinessException(const std::string& m) : AppException(m) {}
};

// ---------- abstract base: runtime polymorphism ----------
class Entity {
protected:
    int id_ = 0;

public:
    virtual ~Entity() = default;
    int id() const noexcept { return id_; }
    void setId(int value) noexcept { id_ = value; }
    virtual std::string tableName() const = 0;
    virtual std::string validate() const = 0;                 // empty == valid
    virtual std::vector<std::string> toRow() const = 0;      // polymorphic export
};

// ---------- Book ----------
class Book : public Entity {
    std::string isbn_;
    std::string title_;
    std::string author_;
    std::string publisher_;
    std::string category_;
    std::string addedAt_;
    int publishYear_ = 0;
    int shelfId_ = 0;
    BookStatus status_ = BookStatus::Available;

public:
    Book() = default;

    const std::string& isbn() const noexcept { return isbn_; }
    const std::string& title() const noexcept { return title_; }
    const std::string& author() const noexcept { return author_; }
    const std::string& publisher() const noexcept { return publisher_; }
    const std::string& category() const noexcept { return category_; }
    const std::string& addedAt() const noexcept { return addedAt_; }
    int publishYear() const noexcept { return publishYear_; }
    int shelfId() const noexcept { return shelfId_; }
    BookStatus status() const noexcept { return status_; }

    void setIsbn(const std::string& v) { isbn_ = trim(v); }
    void setTitle(const std::string& v) { title_ = trim(v); }
    void setAuthor(const std::string& v) { author_ = trim(v); }
    void setPublisher(const std::string& v) { publisher_ = trim(v); }
    void setCategory(const std::string& v) { category_ = trim(v); }
    void setAddedAt(const std::string& v) { addedAt_ = trim(v); }
    void setPublishYear(int v) noexcept { publishYear_ = v; }
    void setShelfId(int v) noexcept { shelfId_ = v; }
    void setStatus(BookStatus v) noexcept { status_ = v; }

    std::string tableName() const override { return "books"; }
    std::string validate() const override;
    std::vector<std::string> toRow() const override;
    bool isAvailable() const noexcept { return status_ == BookStatus::Available; }
    const char* statusText() const noexcept { return toString(status_); }
    void markBorrowed();
    void markReturned();
    static BookStatus statusFromInt(int value);
};

// ---------- Student ----------
class Student : public Entity {
    std::string studentNo_;
    std::string name_;
    std::string major_;
    std::string phone_;
    int maxBorrow_ = 5;
    bool active_ = true;

public:
    Student() = default;

    const std::string& studentNo() const noexcept { return studentNo_; }
    const std::string& name() const noexcept { return name_; }
    const std::string& major() const noexcept { return major_; }
    const std::string& phone() const noexcept { return phone_; }
    int maxBorrow() const noexcept { return maxBorrow_; }
    bool active() const noexcept { return active_; }

    void setStudentNo(const std::string& v) { studentNo_ = trim(v); }
    void setName(const std::string& v) { name_ = trim(v); }
    void setMajor(const std::string& v) { major_ = trim(v); }
    void setPhone(const std::string& v) { phone_ = trim(v); }
    void setMaxBorrow(int v) noexcept { maxBorrow_ = v; }
    void setActive(bool v) noexcept { active_ = v; }

    bool canBorrowMore(int current) const noexcept { return active_ && current < maxBorrow_; }
    static int defaultMaxBorrow() noexcept { return 5; }

    std::string tableName() const override { return "students"; }
    std::string validate() const override;
    std::vector<std::string> toRow() const override;
};

// ---------- Bookshelf (aggregates Book ids in memory) ----------
class Bookshelf : public Entity {
    std::string code_;
    std::string location_;
    std::string category_;
    std::string remark_;
    int capacity_ = 0;
    std::vector<int> bookIds_;

public:
    Bookshelf() = default;

    const std::string& code() const noexcept { return code_; }
    const std::string& location() const noexcept { return location_; }
    const std::string& category() const noexcept { return category_; }
    const std::string& remark() const noexcept { return remark_; }
    int capacity() const noexcept { return capacity_; }
    const std::vector<int>& bookIds() const noexcept { return bookIds_; }

    void setCode(const std::string& v) { code_ = trim(v); }
    void setLocation(const std::string& v) { location_ = trim(v); }
    void setCategory(const std::string& v) { category_ = trim(v); }
    void setRemark(const std::string& v) { remark_ = trim(v); }
    void setCapacity(int v) noexcept { capacity_ = v; }

    std::size_t bookCount() const noexcept { return bookIds_.size(); }
    int remaining() const noexcept { return capacity_ - static_cast<int>(bookIds_.size()); }
    bool canAdd() const noexcept { return remaining() > 0; }
    void addBookId(int id);
    void clearBookIds() noexcept { bookIds_.clear(); }

    std::string tableName() const override { return "bookshelves"; }
    std::string validate() const override;
    std::vector<std::string> toRow() const override;
};

// ---------- BorrowRecord ----------
class BorrowRecord : public Entity {
    int studentId_ = 0;
    int bookId_ = 0;
    std::string borrowDate_;
    std::string dueDate_;
    std::string returnDate_;
    std::string remark_;
    LoanStatus status_ = LoanStatus::Borrowed;

public:
    BorrowRecord() = default;

    int studentId() const noexcept { return studentId_; }
    int bookId() const noexcept { return bookId_; }
    const std::string& borrowDate() const noexcept { return borrowDate_; }
    const std::string& dueDate() const noexcept { return dueDate_; }
    const std::string& returnDate() const noexcept { return returnDate_; }
    const std::string& remark() const noexcept { return remark_; }
    LoanStatus status() const noexcept { return status_; }

    void setStudentId(int v) noexcept { studentId_ = v; }
    void setBookId(int v) noexcept { bookId_ = v; }
    void setBorrowDate(const std::string& v) { borrowDate_ = trim(v); }
    void setDueDate(const std::string& v) { dueDate_ = trim(v); }
    void setReturnDate(const std::string& v) { returnDate_ = trim(v); }
    void setRemark(const std::string& v) { remark_ = trim(v); }
    void setStatus(LoanStatus v) noexcept { status_ = v; }

    bool isOverdue(const std::string& day) const;
    void close(const std::string& day);
    const char* statusText() const noexcept { return toString(status_); }
    static LoanStatus statusFromInt(int value);

    std::string tableName() const override { return "borrow_records"; }
    std::string validate() const override;
    std::vector<std::string> toRow() const override;
};

}  // namespace lls
