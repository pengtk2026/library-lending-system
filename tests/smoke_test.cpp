// smoke_test.cpp -- automated checks for the domain / DAO / service layers
// Mirrors TC-01 ~ TC-11 of the Low Level Design document (section 9.2).
#include "database.h"
#include "service.h"

#include <filesystem>
#include <iostream>
#include <string>

using namespace lls;

namespace {
int passed = 0;
int failed = 0;

void check(const std::string& name, bool ok) {
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << name << '\n';
    if (ok)
        ++passed;
    else
        ++failed;
}
}  // namespace

int main() {
    // ---- pure domain tests ----
    Book invalid;
    invalid.setIsbn("");
    invalid.setTitle("");
    check("TC-01  实体验证拦截空字段", !invalid.validate().empty());
    check("TC-02  日期计算 addDays(2026-01-31, 30)",
          addDays("2026-01-31", 30) == "2026-03-02");

    BorrowRecord overdue;
    overdue.setBorrowDate("2026-09-01");
    overdue.setDueDate("2026-10-01");
    overdue.setStatus(LoanStatus::Borrowed);
    check("TC-03  逾期判定", overdue.isOverdue("2026-11-11"));

    // ---- integration tests on a disposable database ----
    const std::string path = "build/test_library.db";
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);

    try {
        Database database(path);
        LibraryService service(database);
        service.initialize();

        Statistics stats = service.statistics();
        check("TC-04  建库与演示数据（8 书 / 4 生 / 3 架）",
              stats.totalBooks == 8 && stats.totalStudents == 4 && stats.totalShelves == 3);
        check("TC-04b 借阅统计视图（2 借出 / 1 逾期）",
              stats.borrowedBooks == 2 && stats.overdueLoans == 1);

        bool caught = false;
        try {
            Student duplicate;
            duplicate.setStudentNo("20240001");
            duplicate.setName("重复学号");
            service.addStudent(duplicate);
        } catch (const BusinessException&) {
            caught = true;
        }
        check("TC-05  学号唯一性", caught);

        caught = false;
        try {
            service.borrowBook(4, 2);  // student 4 is disabled
        } catch (const BusinessException&) {
            caught = true;
        }
        check("TC-06  停用学生禁止借书", caught);

        caught = false;
        try {
            service.borrowBook(2, 2);  // student 2 has an overdue loan
        } catch (const BusinessException&) {
            caught = true;
        }
        check("TC-10  逾期学生禁止借书", caught);

        caught = false;
        try {
            service.borrowBook(3, 1);  // book 1 is already borrowed
        } catch (const BusinessException&) {
            caught = true;
        }
        check("TC-08  已借出图书禁止再借", caught);

        // student 3 has maxBorrow = 3
        for (int bookId : {2, 3, 5}) service.borrowBook(3, bookId);
        check("TC-07a 连续借书更新库存", service.statistics().borrowedBooks == 5);

        caught = false;
        try {
            service.borrowBook(3, 6);
        } catch (const BusinessException&) {
            caught = true;
        }
        check("TC-07  达到可借上限后拒绝", caught);

        std::vector<LoanDetail> active = service.listLoans(LoanFilter::Active);
        check("TC-09a 当前借阅筛选（5 条）", active.size() == 5);

        int recordId = 0;
        for (const LoanDetail& detail : active) {
            if (detail.record.studentId() == 3 && detail.record.bookId() == 2)
                recordId = detail.recordId();
        }
        service.returnBook(recordId);
        check("TC-09  还书后图书恢复在馆", service.statistics().availableBooks == 4);

        caught = false;
        try {
            service.returnBook(recordId);
        } catch (const BusinessException&) {
            caught = true;
        }
        check("TC-09b 重复归还被拒绝", caught);

        caught = false;
        try {
            service.removeShelf(1);  // shelf 1 still holds books
        } catch (const BusinessException&) {
            caught = true;
        }
        check("TC-11  删除被引用书架被拒绝", caught);

        check("TC-12  检索与统计可用",
              !service.listBooks("C++").empty() && !service.shelfStats().empty());
    } catch (const std::exception& error) {
        std::cout << "[ERROR] " << error.what() << '\n';
        ++failed;
    }

    std::cout << "\nRESULT: " << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
