// service.cpp -- business rules, transaction orchestration and statistics
#include "service.h"

#include "database.h"

#include <sstream>

namespace lls {

std::string Statistics::summary() const {
    std::ostringstream out;
    out << "藏书 " << totalBooks << " 册   在馆 " << availableBooks
        << "   借出 " << borrowedBooks << "   逾期 " << overdueLoans
        << "   学生 " << totalStudents << "   书架 " << totalShelves;
    return out.str();
}

void LibraryService::checkEntity(const Entity& entity) {
    const std::string error = entity.validate();  // virtual dispatch through base reference
    if (!error.empty()) throw ValidationException(error);
}

LibraryService::LibraryService(Database& db)
    : db_(db), bookDao_(db), studentDao_(db), shelfDao_(db), loanDao_(db) {}

void LibraryService::initialize() {
    DbInitializer::createSchema(db_);
    DbInitializer::seedDemoData(db_);
    refreshOverdue();
}

// ---------------------------------------------------------------- books
int LibraryService::addBook(const Book& book) {
    checkEntity(book);
    shelfDao_.findById(book.shelfId());  // foreign key pre-check with friendly error
    Book entity = book;
    if (entity.addedAt().empty()) entity.setAddedAt(today());
    return bookDao_.insert(entity);
}

void LibraryService::updateBook(const Book& book) {
    checkEntity(book);
    if (book.id() <= 0) throw ValidationException("图书 ID 无效");
    bookDao_.findById(book.id());
    shelfDao_.findById(book.shelfId());
    bookDao_.update(book);
}

void LibraryService::removeBook(int id) {
    Book book = bookDao_.findById(id);
    if (loanDao_.activeForBook(id) || book.status() == BookStatus::Borrowed)
        throw BusinessException("该图书存在未归还借阅记录，无法删除");
    if (!bookDao_.removeById(id)) throw NotFoundException("图书不存在");
}

std::vector<Book> LibraryService::listBooks(const std::string& keyword) {
    return trim(keyword).empty() ? bookDao_.listAll() : bookDao_.search(keyword);
}

std::vector<Book> LibraryService::availableBooks() {
    return bookDao_.availableList();
}

// ---------------------------------------------------------------- students
int LibraryService::addStudent(const Student& student) {
    checkEntity(student);
    if (studentDao_.existsNo(student.studentNo(), 0))
        throw BusinessException("学号已存在：" + student.studentNo());
    return studentDao_.insert(student);
}

void LibraryService::updateStudent(const Student& student) {
    checkEntity(student);
    if (student.id() <= 0) throw ValidationException("学生 ID 无效");
    studentDao_.findById(student.id());
    if (studentDao_.existsNo(student.studentNo(), student.id()))
        throw BusinessException("学号已存在：" + student.studentNo());
    studentDao_.update(student);
}

void LibraryService::removeStudent(int id) {
    studentDao_.findById(id);
    if (studentDao_.countActiveLoans(id) > 0)
        throw BusinessException("该学生尚有未归还图书，无法删除");
    if (!studentDao_.removeById(id)) throw NotFoundException("学生不存在");
}

std::vector<Student> LibraryService::listStudents() {
    return studentDao_.listAll();
}

int LibraryService::activeLoanCount(int studentId) {
    return loanDao_.countActiveByStudent(studentId);
}

// ---------------------------------------------------------------- shelves
int LibraryService::addShelf(const Bookshelf& shelf) {
    checkEntity(shelf);
    if (shelfDao_.existsCode(shelf.code(), 0))
        throw BusinessException("书架编号已存在：" + shelf.code());
    return shelfDao_.insert(shelf);
}

void LibraryService::updateShelf(const Bookshelf& shelf) {
    checkEntity(shelf);
    if (shelf.id() <= 0) throw ValidationException("书架 ID 无效");
    shelfDao_.findById(shelf.id());
    if (shelfDao_.existsCode(shelf.code(), shelf.id()))
        throw BusinessException("书架编号已存在：" + shelf.code());
    shelfDao_.update(shelf);
}

void LibraryService::removeShelf(int id) {
    shelfDao_.findById(id);
    if (shelfDao_.countBooks(id) > 0)
        throw BusinessException("该书架上仍有藏书，无法删除");
    if (!shelfDao_.removeById(id)) throw NotFoundException("书架不存在");
}

std::vector<Bookshelf> LibraryService::listShelves() {
    return shelfDao_.listAll();
}

std::vector<ShelfStat> LibraryService::shelfStats() {
    return shelfDao_.listWithStats();
}

// ---------------------------------------------------------------- lending
int LibraryService::borrowBook(int studentId, int bookId) {
    Student student = studentDao_.findById(studentId);
    if (!student.active()) throw BusinessException("该学生已停用，不能借书");
    const std::string day = today();
    if (loanDao_.hasOverdue(studentId, day))
        throw BusinessException("该生存在逾期未还图书，请先归还");
    const int active = loanDao_.countActiveByStudent(studentId);
    if (!student.canBorrowMore(active))
        throw BusinessException("已达可借上限 " + std::to_string(student.maxBorrow()) + " 册");
    Book book = bookDao_.findById(bookId);
    if (!book.isAvailable()) throw BusinessException("该图书当前不可借出");

    BorrowRecord record;
    record.setStudentId(studentId);
    record.setBookId(bookId);
    record.setBorrowDate(day);
    record.setDueDate(addDays(day, 30));
    record.setStatus(LoanStatus::Borrowed);
    record.setRemark("");
    checkEntity(record);

    Transaction transaction(db_);
    const int recordId = loanDao_.insert(record);
    bookDao_.updateStatus(bookId, BookStatus::Borrowed);
    transaction.commit();
    return recordId;
}

void LibraryService::returnBook(int recordId) {
    BorrowRecord record = loanDao_.findById(recordId);
    if (record.status() == LoanStatus::Returned)
        throw BusinessException("该记录已归还");
    const std::string day = today();
    Transaction transaction(db_);
    if (!loanDao_.close(recordId, day))
        throw BusinessException("归还失败：记录状态已变更");
    bookDao_.updateStatus(record.bookId(), BookStatus::Available);
    transaction.commit();
}

std::vector<LoanDetail> LibraryService::listLoans(LoanFilter filter) {
    return loanDao_.listDetails(filter);
}

// ---------------------------------------------------------------- statistics
Statistics LibraryService::statistics() {
    Statistics result;
    Statement statement = db_.prepare(
        "SELECT "
        "(SELECT COUNT(*) FROM books),"
        "(SELECT COUNT(*) FROM books WHERE status=0),"
        "(SELECT COUNT(*) FROM books WHERE status=1),"
        "(SELECT COUNT(*) FROM borrow_records WHERE status<>1 "
        " AND due_date < date('now','localtime')),"
        "(SELECT COUNT(*) FROM students),"
        "(SELECT COUNT(*) FROM bookshelves);");
    if (statement.step()) {
        result.totalBooks = statement.columnInt(0);
        result.availableBooks = statement.columnInt(1);
        result.borrowedBooks = statement.columnInt(2);
        result.overdueLoans = statement.columnInt(3);
        result.totalStudents = statement.columnInt(4);
        result.totalShelves = statement.columnInt(5);
    }
    return result;
}

int LibraryService::refreshOverdue() {
    return loanDao_.markOverdue(today());
}

}  // namespace lls
