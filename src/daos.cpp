// daos.cpp -- SQL statements, parameter binding and row-to-entity mapping
#include "daos.h"

#include <string>

namespace lls {
namespace {

const char* const kBookColumns =
    "SELECT b.id,b.isbn,b.title,b.author,b.publisher,b.publish_year,"
    "b.category,b.shelf_id,b.status,b.added_at FROM books b ";
const char* const kStudentColumns =
    "SELECT id,student_no,name,major,phone,max_borrow,active FROM students ";
const char* const kShelfColumns =
    "SELECT id,code,location,category,capacity,remark FROM bookshelves ";
const char* const kLoanColumns =
    "SELECT id,student_id,book_id,borrow_date,due_date,return_date,status,remark "
    "FROM borrow_records ";
const char* const kDetailColumns =
    "SELECT record_id,student_id,student_no,student_name,book_id,isbn,title,"
    "shelf_code,borrow_date,due_date,return_date,status,remark FROM v_loan_detail ";

Book mapBook(Statement& statement) {
    Book book;
    book.setId(statement.columnInt(0));
    book.setIsbn(statement.columnText(1));
    book.setTitle(statement.columnText(2));
    book.setAuthor(statement.columnText(3));
    book.setPublisher(statement.columnText(4));
    book.setPublishYear(statement.columnInt(5));
    book.setCategory(statement.columnText(6));
    book.setShelfId(statement.columnInt(7));
    book.setStatus(Book::statusFromInt(statement.columnInt(8)));
    book.setAddedAt(statement.columnText(9));
    return book;
}

Student mapStudent(Statement& statement) {
    Student student;
    student.setId(statement.columnInt(0));
    student.setStudentNo(statement.columnText(1));
    student.setName(statement.columnText(2));
    student.setMajor(statement.columnText(3));
    student.setPhone(statement.columnText(4));
    student.setMaxBorrow(statement.columnInt(5));
    student.setActive(statement.columnInt(6) != 0);
    return student;
}

Bookshelf mapShelf(Statement& statement) {
    Bookshelf shelf;
    shelf.setId(statement.columnInt(0));
    shelf.setCode(statement.columnText(1));
    shelf.setLocation(statement.columnText(2));
    shelf.setCategory(statement.columnText(3));
    shelf.setCapacity(statement.columnInt(4));
    shelf.setRemark(statement.columnText(5));
    return shelf;
}

BorrowRecord mapLoan(Statement& statement) {
    BorrowRecord record;
    record.setId(statement.columnInt(0));
    record.setStudentId(statement.columnInt(1));
    record.setBookId(statement.columnInt(2));
    record.setBorrowDate(statement.columnText(3));
    record.setDueDate(statement.columnText(4));
    if (!statement.columnIsNull(5)) record.setReturnDate(statement.columnText(5));
    record.setStatus(BorrowRecord::statusFromInt(statement.columnInt(6)));
    record.setRemark(statement.columnText(7));
    return record;
}

LoanDetail mapDetail(Statement& statement) {
    LoanDetail detail;
    BorrowRecord record;
    record.setId(statement.columnInt(0));
    record.setStudentId(statement.columnInt(1));
    detail.studentNo = statement.columnText(2);
    detail.studentName = statement.columnText(3);
    record.setBookId(statement.columnInt(4));
    detail.isbn = statement.columnText(5);
    detail.bookTitle = statement.columnText(6);
    detail.shelfCode = statement.columnText(7);
    record.setBorrowDate(statement.columnText(8));
    record.setDueDate(statement.columnText(9));
    if (!statement.columnIsNull(10)) record.setReturnDate(statement.columnText(10));
    record.setStatus(BorrowRecord::statusFromInt(statement.columnInt(11)));
    record.setRemark(statement.columnText(12));
    detail.record = record;
    return detail;
}

}  // namespace

// ---------------------------------------------------------------- BookDao
int BookDao::insert(const Book& book) {
    Statement statement = db_.prepare(
        "INSERT INTO books(isbn,title,author,publisher,publish_year,category,"
        "shelf_id,status,added_at) VALUES(?,?,?,?,?,?,?,?,?);");
    statement.bindText(1, book.isbn())
             .bindText(2, book.title())
             .bindText(3, book.author())
             .bindText(4, book.publisher())
             .bindInt(5, book.publishYear())
             .bindText(6, book.category())
             .bindInt(7, book.shelfId())
             .bindInt(8, static_cast<int>(book.status()))
             .bindText(9, book.addedAt().empty() ? today() : book.addedAt());
    statement.execute();
    return static_cast<int>(db_.lastInsertId());
}

void BookDao::update(const Book& book) {
    requirePersisted(book.id());
    Statement statement = db_.prepare(
        "UPDATE books SET isbn=?,title=?,author=?,publisher=?,publish_year=?,"
        "category=?,shelf_id=?,status=?,added_at=? WHERE id=?;");
    statement.bindText(1, book.isbn())
             .bindText(2, book.title())
             .bindText(3, book.author())
             .bindText(4, book.publisher())
             .bindInt(5, book.publishYear())
             .bindText(6, book.category())
             .bindInt(7, book.shelfId())
             .bindInt(8, static_cast<int>(book.status()))
             .bindText(9, book.addedAt().empty() ? today() : book.addedAt())
             .bindInt(10, book.id());
    statement.execute();
}

bool BookDao::removeById(int id) {
    Statement statement = db_.prepare("DELETE FROM books WHERE id=?;");
    statement.bindInt(1, id);
    statement.execute();
    return db_.changes() > 0;
}

Book BookDao::findById(int id) const {
    Statement statement = db_.prepare(std::string(kBookColumns) + "WHERE b.id=?;");
    statement.bindInt(1, id);
    if (!statement.step()) throw NotFoundException("图书 ID=" + std::to_string(id) + " 不存在");
    return mapBook(statement);
}

std::vector<Book> BookDao::listAll() const {
    Statement statement = db_.prepare(std::string(kBookColumns) + "ORDER BY b.id;");
    std::vector<Book> result;
    while (statement.step()) result.push_back(mapBook(statement));
    return result;
}

std::vector<Book> BookDao::search(const std::string& keyword) const {
    Statement statement = db_.prepare(
        std::string(kBookColumns) +
        "LEFT JOIN bookshelves sh ON sh.id=b.shelf_id "
        "WHERE b.title LIKE ? OR b.author LIKE ? OR b.isbn LIKE ? ORDER BY b.id;");
    std::string pattern = "%" + trim(keyword) + "%";
    statement.bindText(1, pattern).bindText(2, pattern).bindText(3, pattern);
    std::vector<Book> result;
    while (statement.step()) result.push_back(mapBook(statement));
    return result;
}

std::vector<Book> BookDao::availableList() const {
    Statement statement = db_.prepare(std::string(kBookColumns) +
                                      "WHERE b.status=0 ORDER BY b.id;");
    std::vector<Book> result;
    while (statement.step()) result.push_back(mapBook(statement));
    return result;
}

void BookDao::updateStatus(int id, BookStatus status) {
    Statement statement = db_.prepare("UPDATE books SET status=? WHERE id=?;");
    statement.bindInt(1, static_cast<int>(status)).bindInt(2, id);
    statement.execute();
}

int BookDao::countByShelf(int shelfId) const {
    Statement statement = db_.prepare("SELECT COUNT(*) FROM books WHERE shelf_id=?;");
    statement.bindInt(1, shelfId);
    return statement.step() ? statement.columnInt(0) : 0;
}

// ---------------------------------------------------------------- StudentDao
int StudentDao::insert(const Student& student) {
    Statement statement = db_.prepare(
        "INSERT INTO students(student_no,name,major,phone,max_borrow,active) "
        "VALUES(?,?,?,?,?,?);");
    statement.bindText(1, student.studentNo())
             .bindText(2, student.name())
             .bindText(3, student.major())
             .bindText(4, student.phone())
             .bindInt(5, student.maxBorrow())
             .bindInt(6, student.active() ? 1 : 0);
    statement.execute();
    return static_cast<int>(db_.lastInsertId());
}

void StudentDao::update(const Student& student) {
    requirePersisted(student.id());
    Statement statement = db_.prepare(
        "UPDATE students SET student_no=?,name=?,major=?,phone=?,max_borrow=?,"
        "active=? WHERE id=?;");
    statement.bindText(1, student.studentNo())
             .bindText(2, student.name())
             .bindText(3, student.major())
             .bindText(4, student.phone())
             .bindInt(5, student.maxBorrow())
             .bindInt(6, student.active() ? 1 : 0)
             .bindInt(7, student.id());
    statement.execute();
}

bool StudentDao::removeById(int id) {
    Statement statement = db_.prepare("DELETE FROM students WHERE id=?;");
    statement.bindInt(1, id);
    statement.execute();
    return db_.changes() > 0;
}

Student StudentDao::findById(int id) const {
    Statement statement = db_.prepare(std::string(kStudentColumns) + "WHERE id=?;");
    statement.bindInt(1, id);
    if (!statement.step()) throw NotFoundException("学生 ID=" + std::to_string(id) + " 不存在");
    return mapStudent(statement);
}

std::vector<Student> StudentDao::listAll() const {
    Statement statement = db_.prepare(std::string(kStudentColumns) + "ORDER BY id;");
    std::vector<Student> result;
    while (statement.step()) result.push_back(mapStudent(statement));
    return result;
}

bool StudentDao::findByStudentNo(const std::string& no, Student& out) const {
    Statement statement = db_.prepare(std::string(kStudentColumns) + "WHERE student_no=?;");
    statement.bindText(1, no);
    if (!statement.step()) return false;
    out = mapStudent(statement);
    return true;
}

bool StudentDao::existsNo(const std::string& no, int excludeId) const {
    Statement statement = db_.prepare(
        "SELECT COUNT(*) FROM students WHERE student_no=? AND id<>?;");
    statement.bindText(1, no).bindInt(2, excludeId);
    return statement.step() && statement.columnInt(0) > 0;
}

int StudentDao::countActiveLoans(int studentId) const {
    Statement statement = db_.prepare(
        "SELECT COUNT(*) FROM borrow_records WHERE student_id=? AND status IN (0,2);");
    statement.bindInt(1, studentId);
    return statement.step() ? statement.columnInt(0) : 0;
}

// ---------------------------------------------------------------- BookshelfDao
int BookshelfDao::insert(const Bookshelf& shelf) {
    Statement statement = db_.prepare(
        "INSERT INTO bookshelves(code,location,category,capacity,remark) "
        "VALUES(?,?,?,?,?);");
    statement.bindText(1, shelf.code())
             .bindText(2, shelf.location())
             .bindText(3, shelf.category())
             .bindInt(4, shelf.capacity())
             .bindText(5, shelf.remark());
    statement.execute();
    return static_cast<int>(db_.lastInsertId());
}

void BookshelfDao::update(const Bookshelf& shelf) {
    requirePersisted(shelf.id());
    Statement statement = db_.prepare(
        "UPDATE bookshelves SET code=?,location=?,category=?,capacity=?,remark=? "
        "WHERE id=?;");
    statement.bindText(1, shelf.code())
             .bindText(2, shelf.location())
             .bindText(3, shelf.category())
             .bindInt(4, shelf.capacity())
             .bindText(5, shelf.remark())
             .bindInt(6, shelf.id());
    statement.execute();
}

bool BookshelfDao::removeById(int id) {
    Statement statement = db_.prepare("DELETE FROM bookshelves WHERE id=?;");
    statement.bindInt(1, id);
    statement.execute();
    return db_.changes() > 0;
}

Bookshelf BookshelfDao::findById(int id) const {
    Statement statement = db_.prepare(std::string(kShelfColumns) + "WHERE id=?;");
    statement.bindInt(1, id);
    if (!statement.step()) throw NotFoundException("书架 ID=" + std::to_string(id) + " 不存在");
    return mapShelf(statement);
}

std::vector<Bookshelf> BookshelfDao::listAll() const {
    Statement statement = db_.prepare(std::string(kShelfColumns) + "ORDER BY id;");
    std::vector<Bookshelf> result;
    while (statement.step()) result.push_back(mapShelf(statement));
    return result;
}

bool BookshelfDao::existsCode(const std::string& code, int excludeId) const {
    Statement statement = db_.prepare(
        "SELECT COUNT(*) FROM bookshelves WHERE code=? AND id<>?;");
    statement.bindText(1, code).bindInt(2, excludeId);
    return statement.step() && statement.columnInt(0) > 0;
}

int BookshelfDao::countBooks(int shelfId) const {
    Statement statement = db_.prepare("SELECT COUNT(*) FROM books WHERE shelf_id=?;");
    statement.bindInt(1, shelfId);
    return statement.step() ? statement.columnInt(0) : 0;
}

std::vector<ShelfStat> BookshelfDao::listWithStats() const {
    Statement statement = db_.prepare(
        "SELECT id,code,location,category,capacity,remark,book_count,available_count "
        "FROM v_shelf_stat ORDER BY id;");
    std::vector<ShelfStat> result;
    while (statement.step()) {
        ShelfStat stat;
        stat.shelf.setId(statement.columnInt(0));
        stat.shelf.setCode(statement.columnText(1));
        stat.shelf.setLocation(statement.columnText(2));
        stat.shelf.setCategory(statement.columnText(3));
        stat.shelf.setCapacity(statement.columnInt(4));
        stat.shelf.setRemark(statement.columnText(5));
        stat.bookCount = statement.columnInt(6);
        stat.availableCount = statement.columnInt(7);
        result.push_back(stat);
    }
    return result;
}

// ---------------------------------------------------------------- BorrowRecordDao
int BorrowRecordDao::insert(const BorrowRecord& record) {
    Statement statement = db_.prepare(
        "INSERT INTO borrow_records(student_id,book_id,borrow_date,due_date,status,remark) "
        "VALUES(?,?,?,?,?,?);");
    statement.bindInt(1, record.studentId())
             .bindInt(2, record.bookId())
             .bindText(3, record.borrowDate())
             .bindText(4, record.dueDate())
             .bindInt(5, static_cast<int>(record.status()))
             .bindText(6, record.remark());
    statement.execute();
    return static_cast<int>(db_.lastInsertId());
}

void BorrowRecordDao::update(const BorrowRecord& record) {
    requirePersisted(record.id());
    Statement statement = db_.prepare(
        "UPDATE borrow_records SET student_id=?,book_id=?,borrow_date=?,due_date=?,"
        "return_date=?,status=?,remark=? WHERE id=?;");
    statement.bindInt(1, record.studentId())
             .bindInt(2, record.bookId())
             .bindText(3, record.borrowDate())
             .bindText(4, record.dueDate());
    if (record.returnDate().empty())
        statement.bindNull(5);
    else
        statement.bindText(5, record.returnDate());
    statement.bindInt(6, static_cast<int>(record.status()))
             .bindText(7, record.remark())
             .bindInt(8, record.id());
    statement.execute();
}

bool BorrowRecordDao::removeById(int id) {
    Statement statement = db_.prepare("DELETE FROM borrow_records WHERE id=?;");
    statement.bindInt(1, id);
    statement.execute();
    return db_.changes() > 0;
}

BorrowRecord BorrowRecordDao::findById(int id) const {
    Statement statement = db_.prepare(std::string(kLoanColumns) + "WHERE id=?;");
    statement.bindInt(1, id);
    if (!statement.step()) throw NotFoundException("借阅记录 ID=" + std::to_string(id) + " 不存在");
    return mapLoan(statement);
}

std::vector<BorrowRecord> BorrowRecordDao::listAll() const {
    Statement statement = db_.prepare(std::string(kLoanColumns) +
                                      "ORDER BY borrow_date DESC, id DESC;");
    std::vector<BorrowRecord> result;
    while (statement.step()) result.push_back(mapLoan(statement));
    return result;
}

bool BorrowRecordDao::close(int id, const std::string& returnDate) {
    Statement statement = db_.prepare(
        "UPDATE borrow_records SET return_date=?,status=1 WHERE id=? AND status<>1;");
    statement.bindText(1, returnDate).bindInt(2, id);
    statement.execute();
    return db_.changes() > 0;
}

std::vector<LoanDetail> BorrowRecordDao::listDetails(LoanFilter filter) const {
    std::string sql = kDetailColumns;
    switch (filter) {
        case LoanFilter::Active:
            sql += "WHERE status IN (0,2) ";
            break;
        case LoanFilter::Overdue:
            sql += "WHERE status<>1 AND due_date < date('now','localtime') ";
            break;
        case LoanFilter::All:
        default:
            break;
    }
    sql += "ORDER BY borrow_date DESC, record_id DESC;";
    Statement statement = db_.prepare(sql);
    std::vector<LoanDetail> result;
    while (statement.step()) result.push_back(mapDetail(statement));
    return result;
}

std::vector<LoanDetail> BorrowRecordDao::listDetails() const {
    return listDetails(LoanFilter::All);
}

int BorrowRecordDao::countActiveByStudent(int studentId) const {
    Statement statement = db_.prepare(
        "SELECT COUNT(*) FROM borrow_records WHERE student_id=? AND status IN (0,2);");
    statement.bindInt(1, studentId);
    return statement.step() ? statement.columnInt(0) : 0;
}

bool BorrowRecordDao::hasOverdue(int studentId, const std::string& day) const {
    Statement statement = db_.prepare(
        "SELECT COUNT(*) FROM borrow_records WHERE student_id=? AND status<>1 "
        "AND due_date < ?;");
    statement.bindInt(1, studentId).bindText(2, day);
    return statement.step() && statement.columnInt(0) > 0;
}

bool BorrowRecordDao::activeForBook(int bookId) const {
    Statement statement = db_.prepare(
        "SELECT COUNT(*) FROM borrow_records WHERE book_id=? AND status IN (0,2);");
    statement.bindInt(1, bookId);
    return statement.step() && statement.columnInt(0) > 0;
}

int BorrowRecordDao::markOverdue(const std::string& day) {
    Statement statement = db_.prepare(
        "UPDATE borrow_records SET status=2 WHERE status=0 AND due_date < ?;");
    statement.bindText(1, day);
    statement.execute();
    return db_.changes();
}

}  // namespace lls
