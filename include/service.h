#pragma once
// service.h -- business facade: validation, rules, transaction boundaries
#include "daos.h"

#include <string>
#include <vector>

namespace lls {

struct Statistics {
    int totalBooks = 0;
    int availableBooks = 0;
    int borrowedBooks = 0;
    int overdueLoans = 0;
    int totalStudents = 0;
    int totalShelves = 0;
    std::string summary() const;
};

class LibraryService {
    Database& db_;
    BookDao bookDao_;
    StudentDao studentDao_;
    BookshelfDao shelfDao_;
    BorrowRecordDao loanDao_;

    static void checkEntity(const Entity& entity);

public:
    explicit LibraryService(Database& db);

    void initialize();

    int addBook(const Book& book);
    void updateBook(const Book& book);
    void removeBook(int id);
    std::vector<Book> listBooks(const std::string& keyword = "");
    std::vector<Book> availableBooks();

    int addStudent(const Student& student);
    void updateStudent(const Student& student);
    void removeStudent(int id);
    std::vector<Student> listStudents();
    int activeLoanCount(int studentId);

    int addShelf(const Bookshelf& shelf);
    void updateShelf(const Bookshelf& shelf);
    void removeShelf(int id);
    std::vector<Bookshelf> listShelves();
    std::vector<ShelfStat> shelfStats();

    int borrowBook(int studentId, int bookId);
    void returnBook(int recordId);
    std::vector<LoanDetail> listLoans(LoanFilter filter);

    Statistics statistics();
    int refreshOverdue();
};

}  // namespace lls
