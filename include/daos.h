#pragma once
// daos.h -- data access objects and read models shared with the service layer
#include "database.h"
#include "domain.h"

#include <string>
#include <vector>

namespace lls {

// Template base class: common contract + reusable helpers, SQL supplied by derived DAOs.
template <typename T>
class Dao {
protected:
    Database& db_;

    static void requirePersisted(int id) {
        if (id <= 0) throw ValidationException("记录 ID 无效");
    }

public:
    explicit Dao(Database& db) : db_(db) {}
    virtual ~Dao() = default;
    virtual int insert(const T& entity) = 0;
    virtual void update(const T& entity) = 0;
    virtual bool removeById(int id) = 0;
    virtual T findById(int id) const = 0;
    virtual std::vector<T> listAll() const = 0;
};

struct LoanDetail {
    BorrowRecord record;
    std::string studentNo;
    std::string studentName;
    std::string isbn;
    std::string bookTitle;
    std::string shelfCode;
    int recordId() const noexcept { return record.id(); }
    const char* statusText() const noexcept { return record.statusText(); }
};

struct ShelfStat {
    Bookshelf shelf;
    int bookCount = 0;
    int availableCount = 0;
};

class BookDao : public Dao<Book> {
public:
    using Dao<Book>::Dao;
    int insert(const Book& book) override;
    void update(const Book& book) override;
    bool removeById(int id) override;
    Book findById(int id) const override;
    std::vector<Book> listAll() const override;
    std::vector<Book> search(const std::string& keyword) const;
    std::vector<Book> availableList() const;
    void updateStatus(int id, BookStatus status);
    int countByShelf(int shelfId) const;
};

class StudentDao : public Dao<Student> {
public:
    using Dao<Student>::Dao;
    int insert(const Student& student) override;
    void update(const Student& student) override;
    bool removeById(int id) override;
    Student findById(int id) const override;
    std::vector<Student> listAll() const override;
    bool findByStudentNo(const std::string& no, Student& out) const;
    bool existsNo(const std::string& no, int excludeId) const;
    int countActiveLoans(int studentId) const;
};

class BookshelfDao : public Dao<Bookshelf> {
public:
    using Dao<Bookshelf>::Dao;
    int insert(const Bookshelf& shelf) override;
    void update(const Bookshelf& shelf) override;
    bool removeById(int id) override;
    Bookshelf findById(int id) const override;
    std::vector<Bookshelf> listAll() const override;
    bool existsCode(const std::string& code, int excludeId) const;
    int countBooks(int shelfId) const;
    std::vector<ShelfStat> listWithStats() const;
};

class BorrowRecordDao : public Dao<BorrowRecord> {
public:
    using Dao<BorrowRecord>::Dao;
    int insert(const BorrowRecord& record) override;
    void update(const BorrowRecord& record) override;
    bool removeById(int id) override;
    BorrowRecord findById(int id) const override;
    std::vector<BorrowRecord> listAll() const override;
    bool close(int id, const std::string& returnDate);
    std::vector<LoanDetail> listDetails(LoanFilter filter) const;
    std::vector<LoanDetail> listDetails() const;
    int countActiveByStudent(int studentId) const;
    bool hasOverdue(int studentId, const std::string& day) const;
    bool activeForBook(int bookId) const;
    int markOverdue(const std::string& day);
};

}  // namespace lls
