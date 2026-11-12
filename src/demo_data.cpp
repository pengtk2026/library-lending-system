/**
 * @file demo_data.cpp
 * @brief 可复现的大批量演示数据生成器：1000 册图书 / 100 名学生 / 500 条借阅记录。
 *
 * @section design 设计说明
 * - 定位：不是一次性导入脚本，而是系统内置功能；对应声明 include/demo_data.h，
 *   由 DemoDataTool.exe 与 `LibraryApp.exe --generate-demo` 两个入口调用。
 * - 目标规模与构成：
 *   20 个书架（每架容量 60，共 1200 册位，实际每架 50 册）、100 名学生、
 *   1000 册图书（在馆 800 / 借出 200）、500 条借阅记录
 *   （当前未还 200，其中逾期 40；历史已归还 300）。
 * - 可复现性：全部数据由**确定性公式**生成（不使用随机数），
 *   同一天多次运行得到相同的 ID、姓名、书名与日期分布，便于课堂演示与回归对比。
 * - 主要算法：
 *   1) 先规划「当前借阅」占用的 200 个互不重复的图书 ID（步长 = books/currentLoans），
 *      这些书的 books.status 置 1，其余置 0；
 *   2) 用「轮转 + 可借上限检查」为学生分配当前借阅，保证任一学生的未归还数
 *      不超过 max_borrow（3～8），符合 BR-01；
 *   3) 逾期记录直接把 borrow_date 设在 35～74 天前，使 due_date 落在过去；
 *      正常记录的 due_date 落在未来；
 *   4) 历史记录只从「未被当前借出的图书」中选取，避免与部分唯一索引
 *      uq_active_loan 冲突（同一册书同时只能有一条 status IN (0,2) 的记录）；
 *   5) 四张表在**一个事务**内清空并按外键依赖顺序重建，异常自动回滚；
 *      批量插入复用 Statement::reset()，只编译一次 SQL，性能远优于逐条拼接。
 * - ISBN 算法：EAN-13 前 12 位由固定前缀 + 序号构成，按 1/3 交替权重求和，
 *   校验位 = (10 - sum % 10) % 10，保证 1000 个 ISBN 均唯一且形式合法。
 * - 一致性自检：verify() 用 4 条反例计数 SQL + PRAGMA foreign_key_check
 *   检查「图书状态 ↔ 未归还记录」「可借上限」「逾期日期」「外键完整性」。
 *
 * @author 课程设计组
 * @date 2026-11-12
 */

#include "demo_data.h"

#include "domain.h"      // today() / addDays() / 异常类型 / 实体枚举

#include <algorithm>     // std::min / std::max
#include <fstream>       // 预留：报告落盘由调用方完成
#include <iomanip>       // std::setw / std::setfill：定宽补零
#include <set>           // std::set：当前借阅图书 ID 去重
#include <sstream>       // std::ostringstream：字符串格式化
#include <string>        // std::string
#include <vector>        // std::vector：ID 计划表与标签表

namespace lls {
namespace {  // 匿名命名空间：数据字典与生成辅助函数，仅本文件可见

/** @brief 40 个学科主题，与 5 个变体、5 个分册组合出 1000 个互异题名。 */
const char* const kTopics[] = {
    "C++ 程序设计", "数据结构与算法", "数据库系统原理", "操作系统", "计算机网络",
    "软件工程", "人工智能导论", "Python 数据分析", "编译原理", "计算机组成原理",
    "微观经济学", "宏观经济学", "计量经济学", "财务分析", "会计学",
    "审计学", "管理学原理", "战略管理", "组织行为学", "人力资源管理",
    "市场营销学", "运营管理", "统计学", "概率论与数理统计", "线性代数",
    "高等数学", "中国古代文学", "中国现代文学", "外国文学史", "语言学概论",
    "英语写作", "翻译理论与实践", "社会学概论", "法学导论", "心理学基础",
    "教育学原理", "艺术鉴赏", "中外建筑史", "环境科学概论", "大学物理"};

/** @brief 5 种题名变体（案例教程 / 习题精解 / 实践指南 / 研究前沿 / 精读本）。 */
const char* const kVariants[] = {"案例教程", "习题精解", "实践指南", "研究前沿", "精读本"};

/** @brief 20 个作者名，用于轮转填充图书作者字段。 */
const char* const kAuthors[] = {
    "张先治", "严蔚敏", "周三多", "谭浩强", "王珊", "萨师煊", "谢希仁",
    "汤小丹", "周志华", "李航", "高鸿业", "曼昆", "平狄克", "罗斯",
    "注册会计师协会", "罗宾斯", "科特勒", "盛骤", "同济大学数学系", "袁行霈"};

/** @brief 8 个出版社名称。 */
const char* const kPublishers[] = {
    "清华大学出版社", "机械工业出版社", "人民邮电出版社", "电子工业出版社",
    "东北财经大学出版社", "中国人民大学出版社", "高等教育出版社", "人民文学出版社"};

/** @brief 6 个图书/书架分类，与 kTopics 的学科分布大致对应。 */
const char* const kCategories[] = {
    "计算机", "经济管理", "文学", "社会科学", "数理基础", "艺术与综合"};

/** @brief 30 个姓氏，与 kGivenNames 组合成 100 个不重复姓名。 */
const char* const kSurnames[] = {
    "王", "李", "张", "刘", "陈", "杨", "黄", "赵", "周", "吴",
    "徐", "孙", "马", "朱", "胡", "郭", "何", "高", "林", "罗",
    "郑", "梁", "谢", "宋", "唐", "许", "韩", "冯", "邓", "曹"};

/** @brief 40 个名字，配合 30 个姓氏生成 100 个唯一姓名（i = q*30 + r）。 */
const char* const kGivenNames[] = {
    "明", "华", "芳", "强", "静", "敏", "秀英", "丽", "磊", "洋",
    "艳", "勇", "军", "杰", "娟", "涛", "超", "秀兰", "霞", "平",
    "刚", "桂英", "文", "辉", "娜", "健", "俊", "宇", "欣", "悦",
    "琳", "睿", "嘉", "宁", "彤", "昊", "然", "晨", "曦", "桐"};

/** @brief 8 个专业名称，用于学生专业班级字段。 */
const char* const kMajors[] = {
    "经济学", "人力资源管理", "工商管理", "信息管理",
    "会计学", "金融学", "市场营销", "数据科学"};

/**
 * @brief 把整数格式化为定宽、左侧补零的字符串。
 * @param value 待格式化整数。
 * @param width 目标宽度。
 * @return 补零后的字符串，如 pad(7, 4) → "0007"。
 * @note 用于学号、书架编号、ISBN 片段与电话号码，保证定长与字典序稳定。
 */
std::string pad(int value, int width) {
    std::ostringstream out;                                   ///< 输出流
    out << std::setw(width) << std::setfill('0') << value;   ///< 定宽 + 补零
    return out.str();
}

/**
 * @brief 生成带合法校验位的 EAN-13 ISBN。
 * @param index 图书序号 0..999。
 * @return 13 位数字字符串。
 * @note 主要算法：
 *       1) 前缀固定 "9787"（图书类 EAN 前缀）；
 *       2) 中间 4 位 = 1000 + index/100，后 4 位 = (index*7+1234)%10000，共 12 位；
 *       3) 对 12 位按 1、3 交替权重求和 sum；
 *       4) 校验位 = (10 - sum % 10) % 10，拼到末尾。
 *       该公式保证 1000 本互不重复，且每个 ISBN 都通过标准校验。
 */
std::string makeIsbn(int index) {
    std::ostringstream base;                                  ///< 前 12 位构造流
    base << "9787"                                           ///< 4 位固定前缀
         << pad(1000 + index / 100, 4)                       ///< 4 位组号
         << pad((index * 7 + 1234) % 10000, 4);              ///< 4 位序号
    std::string prefix = base.str();                          ///< 12 位数字
    int sum = 0;                                              ///< 加权和
    for (std::size_t i = 0; i < prefix.size(); ++i)
        sum += (prefix[i] - '0') * (i % 2 == 0 ? 1 : 3);     ///< 权重 1/3 交替
    return prefix + std::to_string((10 - sum % 10) % 10);    ///< 拼上校验位
}

/**
 * @brief 生成互不重复的图书题名。
 * @param index 图书序号 0..999。
 * @return 形如「C++ 程序设计 · 案例教程 · 第1分册」。
 * @note 算法：三个正交维度——主题 index%40、变体 (index/40)%5、分册 index/200+1，
 *       40 × 5 × 5 = 1000，恰好一一对应全部序号，保证题名唯一；
 *       同时保留学科关键字，便于课堂演示检索（如输入「财务」「C++」）。
 */
std::string makeTitle(int index) {
    std::string title = kTopics[index % 40];                  ///< 维度 1：学科主题
    title += " · ";
    title += kVariants[(index / 40) % 5];                     ///< 维度 2：变体
    title += " · 第" + std::to_string(index / 200 + 1) + "分册";  ///< 维度 3：分册
    return title;
}

/** @brief 生成 11 位演示手机号：固定 138 段 + 8 位确定性序号。 */
std::string makePhone(int index) {
    return "138" + pad((10000000 + index * 7919) % 100000000, 8);  ///< 7919 为质数，分散度好
}

/**
 * @brief 执行返回单个整数的 SQL（一致性校验的计数查询）。
 * @param db 数据库连接。
 * @param sql 返回一行一列的 SELECT。
 * @return 计数值；无结果返回 0。
 */
int scalar(Database& db, const std::string& sql) {
    Statement statement = db.prepare(sql);                    ///< 预编译查询
    return statement.step() ? statement.columnInt(0) : 0;    ///< 读取首列
}

/**
 * @brief 生成一致性校验报告文本。
 * @param db 已生成数据的连接。
 * @param report 输出流，追加 PASS/FAIL 明细。
 * @return 全部通过返回 true。
 * @note 主要算法（反例计数）：用「查询异常记录条数」代替断言，
 *       每条 SQL 期望返回 0；另外用 PRAGMA foreign_key_check 逐行统计外键问题。
 *       这样即使数据被后续操作改动，也能随时复核一致性。
 */
bool consistencyChecks(Database& db, std::ostringstream& report) {
    const char* checks[][2] = {                               ///< [说明, 反例计数 SQL]
        {"图书状态与未归还记录一致（借出但无记录）",
         "SELECT COUNT(*) FROM books b WHERE b.status=1 AND NOT EXISTS "
         "(SELECT 1 FROM borrow_records r WHERE r.book_id=b.id AND r.status IN (0,2))"},
        {"未归还记录对应的图书确为借出状态",
         "SELECT COUNT(*) FROM borrow_records r WHERE r.status IN (0,2) "
         "AND (SELECT status FROM books WHERE id=r.book_id)<>1"},
        {"学生当前借阅数不超过可借上限",
         "SELECT COUNT(*) FROM (SELECT student_id, COUNT(*) AS c FROM borrow_records "
         "WHERE status IN (0,2) GROUP BY student_id) t "
         "WHERE t.c > (SELECT max_borrow FROM students WHERE id=t.student_id)"},
        {"逾期记录应还日期确实早于今天",
         "SELECT COUNT(*) FROM borrow_records WHERE status=2 "
         "AND due_date >= date('now','localtime')"},
    };
    bool allOk = true;                                        ///< 总结果
    report << "\n[一致性校验]\n";
    for (int i = 0; i < 4; ++i) {                            ///< 4 条计数型检查
        int bad = scalar(db, checks[i][1]);                   ///< 反例条数，期望 0
        report << (bad == 0 ? "  PASS  " : "  FAIL  ")
               << checks[i][0] << "（异常 " << bad << " 条）\n";
        if (bad != 0) allOk = false;                         ///< 任一条异常即整体失败
    }
    Statement fk = db.prepare("PRAGMA foreign_key_check;");   ///< SQLite 外键完整性检查
    int fkProblems = 0;                                      ///< 外键问题行数
    while (fk.step()) ++fkProblems;                          ///< 每返回一行即一个问题
    report << (fkProblems == 0 ? "  PASS  " : "  FAIL  ")
           << "外键完整性（异常 " << fkProblems << " 条）\n";
    return allOk && fkProblems == 0;
}

}  // namespace 匿名命名空间结束

/**
 * @brief 生成单行规模摘要（用于控制台输出）。
 * @return 如「书架 20 / 学生 100 / 图书 1000（在馆 800，借出 200）…」。
 */
std::string DemoDataReport::summary() const {
    std::ostringstream out;                                   ///< 输出流
    out << "书架 " << shelves << " / 学生 " << students << " / 图书 " << books
        << "（在馆 " << available << "，借出 " << borrowed << "） / 借阅记录 "
        << loans << "（逾期 " << overdue << "）";
    return out.str();
}

/**
 * @brief 生成多行详细报告（写入 data/demo_data_report.txt）。
 * @return 含生成时间、各类数量与未还/已还拆分的文本。
 * @note 「当前未还」= borrowed（借出图书数），「历史已归还」= loans - borrowed，
 *       该恒等式由生成算法保证。
 */
std::string DemoDataReport::detail() const {
    std::ostringstream out;                                   ///< 输出流
    out << "简易图书借阅系统 · 大批量演示数据报告\n"
        << "==========================================\n"
        << "生成时间：" << today() << "\n\n"
        << "[数据规模]\n"
        << "  书架：    " << shelves << " 个（每架容量 60 册）\n"
        << "  学生：    " << students << " 人\n"
        << "  图书：    " << books << " 册（在馆 " << available
        << " / 借出 " << borrowed << "）\n"
        << "  借阅记录：" << loans << " 条（逾期 " << overdue << " 条）\n"
        << "  当前未还：" << borrowed << " 条；历史已归还："
        << (loans - borrowed) << " 条\n";
    return out.str();
}

/**
 * @brief 对外暴露的一致性校验入口。
 * @param db 已生成数据的连接。
 * @return 可读的 PASS/FAIL 报告文本。
 */
std::string DemoDataGenerator::verify(Database& db) {
    std::ostringstream out;                                   ///< 报告缓冲
    consistencyChecks(db, out);                              ///< 执行内部校验
    return out.str();
}

/**
 * @brief 重建全部业务数据（核心生成流程）。
 * @param db 已打开的连接。
 * @param books 目标图书数（当前调用为 1000）。
 * @param students 目标学生数（当前调用为 100）。
 * @param loans 目标借阅记录数（当前调用为 500）。
 * @return 生成后的统计报告。
 * @exception ValidationException 规模参数过小。
 * @exception BusinessException 没有可用于历史借阅的图书。
 *
 * @note 主要算法（分四阶段）：
 *   @code
 *   A 规划：currentLoans = loans*2/5；按步长挑互不重复的图书 ID 作为「当前借出」集合；
 *           建立 isBorrowed 位图；准备每生 maxBorrow 与已分配计数 used。
 *   B 事务开始：按 borrow_records → books → students → bookshelves 顺序清空（外键安全），
 *           并重置 sqlite_sequence 让 ID 从 1 开始。
 *   C 插入：书架(20) → 学生(100) → 图书(1000) → 借阅记录(500)，
 *           每类只 prepare 一次、循环内 bind + execute + reset。
 *   D 提交并统计：一条 7 列子查询 SQL 取回全部计数，填充 DemoDataReport。
 *   @endcode
 *   任一阶段抛异常，Transaction 析构自动 ROLLBACK，数据库保持原状。
 */
DemoDataReport DemoDataGenerator::generate(Database& db, int books,
                                           int students, int loans) {
    if (books < 10 || students < 5 || loans < 1)
        throw ValidationException("演示数据规模参数不合法");  ///< 规模下限保护

    DbInitializer::createSchema(db);                         ///< 幂等建表，兼容全新数据库
    const std::string day = today();                          ///< 全批次统一基准日期

    // ---------- 阶段 A：规划当前借阅与库存占位 ----------
    const int shelfCount = 20;                              ///< 20 * 60 = 1200 个册位
    const int currentLoans = std::min(loans * 2 / 5, books);///< 500 → 200 条当前未还
    const int historyLoans = loans - currentLoans;           ///< 500 → 300 条历史已还
    const int step = std::max(1, books / std::max(1, currentLoans));  ///< 1000/200 = 5

    std::vector<int> currentBooks;                           ///< 当前借阅占用的图书 ID 列表
    std::set<int> reserved;                                  ///< 去重集合（防部分唯一索引冲突）
    for (int k = 0; k < currentLoans; ++k) {
        int id = 1 + (k * step) % books;                     ///< 等间隔取书：1,6,11,...
        while (reserved.count(id)) id = 1 + (id % books);    ///< 冲突时顺序后移（环回）
        reserved.insert(id);
        currentBooks.push_back(id);
    }
    std::vector<char> isBorrowed(books + 1, 0);              ///< 位图：该书是否被当前借出
    for (int id : currentBooks) isBorrowed[id] = 1;          ///< 借出的书 status 置 1

    std::vector<int> maxBorrow(students + 1, 5);             ///< 每生可借上限（1-based 下标）
    std::vector<int> used(students + 1, 0);                  ///< 每生已分配的当前借阅数
    for (int i = 1; i <= students; ++i)
        maxBorrow[i] = 3 + ((i - 1) % 6);                    ///< 上限 3..8 循环分布

    /**
     * @brief 按「轮转 + 上限检查」挑选一个可再借的学生。
     * @param seed 起始偏移，使分配尽量均匀。
     * @return 学生 ID。
     * @note 算法：从 seed 起线性扫描最多 students 次，找到第一个
     *       used[id] < maxBorrow[id] 的学生并累加计数；配置比例下必然找到。
     */
    auto pickStudent = [&](int seed) {
        for (int t = 0; t < students; ++t) {
            int id = 1 + (seed + t) % students;              ///< 轮转候选 ID
            if (used[id] < maxBorrow[id]) {                  ///< 未达上限
                ++used[id];                                  ///< 占用一个名额
                return id;
            }
        }
        return 1;                                            ///< 理论上不可达的兜底
    };

    // ---------- 阶段 B：事务内清空旧数据 ----------
    Transaction transaction(db);                             ///< BEGIN IMMEDIATE
    db.exec("DELETE FROM borrow_records;");                  ///< 先删引用方
    db.exec("DELETE FROM books;");
    db.exec("DELETE FROM students;");
    db.exec("DELETE FROM bookshelves;");                     ///< 最后删被引用方
    db.exec("DELETE FROM sqlite_sequence;");                 ///< 重置自增计数，ID 从 1 开始

    // ---------- 阶段 C1：插入 20 个书架 ----------
    {
        Statement insert = db.prepare(                        ///< 复用同一预编译语句
            "INSERT INTO bookshelves(code,location,category,capacity,remark) "
            "VALUES(?,?,?,?,?);");
        for (int k = 0; k < shelfCount; ++k) {
            char zone = static_cast<char>('A' + k / 5);      ///< A/B/C/D 四个区
            int number = k % 5 + 1;                          ///< 每区 1..5 号架
            std::string code = std::string(1, zone) + "-" + pad(number, 2);  ///< 如 A-01
            std::string location = std::to_string(k / 10 + 1) + "层" + zone + "区"
                                   + pad(number, 2) + "号架";///< 楼层 + 区 + 架号
            insert.bindText(1, code)                         ///< 编号
                  .bindText(2, location)                     ///< 位置
                  .bindText(3, kCategories[k % 6])           ///< 分类
                  .bindInt(4, 60)                            ///< 容量 60（1000/20=50，留余量）
                  .bindText(5, "演示书架");                   ///< 备注
            insert.execute();                                ///< 执行
            insert.reset();                                  ///< 复位以便下一轮绑定
        }
    }

    // ---------- 阶段 C2：插入 100 名学生 ----------
    {
        Statement insert = db.prepare(
            "INSERT INTO students(student_no,name,major,phone,max_borrow,active) "
            "VALUES(?,?,?,?,?,?);");
        for (int i = 0; i < students; ++i) {
            std::string name = std::string(kSurnames[i % 30]) + kGivenNames[(i / 30) % 40];  ///< 姓+名唯一组合
            std::string major = std::string(kMajors[i % 8]) + " "
                                + std::to_string(2021 + i % 4) + " 级";  ///< 专业 + 年级
            insert.bindText(1, "2024" + pad(i + 1, 4))       ///< 学号 20240001..20240100
                  .bindText(2, name)
                  .bindText(3, major)
                  .bindText(4, makePhone(i))                  ///< 演示电话
                  .bindInt(5, maxBorrow[i + 1])               ///< 与分配算法一致的上限
                  .bindInt(6, i % 20 == 19 ? 0 : 1);          ///< 每 20 人停用 1 人（共 5 人）
            insert.execute();
            insert.reset();
        }
    }

    // ---------- 阶段 C3：插入 1000 册图书 ----------
    {
        Statement insert = db.prepare(
            "INSERT INTO books(isbn,title,author,publisher,publish_year,category,"
            "shelf_id,status,added_at) VALUES(?,?,?,?,?,?,?,?,?);");
        for (int i = 0; i < books; ++i) {
            int shelfId = 1 + i % shelfCount;                 ///< 均匀分布：每架 50 册
            std::string author = kAuthors[i % 20];            ///< 轮转作者
            if (i % 3 == 0) author += " 等";                  ///< 部分标注「等」增加真实感
            insert.bindText(1, makeIsbn(i))                   ///< 合法 EAN-13
                  .bindText(2, makeTitle(i))                  ///< 唯一题名
                  .bindText(3, author)
                  .bindText(4, kPublishers[i % 8])
                  .bindInt(5, 1995 + i % 30)                  ///< 出版年 1995..2024
                  .bindText(6, kCategories[(shelfId - 1) % 6])///< 与书架分类一致
                  .bindInt(7, shelfId)                        ///< 外键
                  .bindInt(8, isBorrowed[i + 1] ? 1 : 0)      ///< 关键：与当前借阅计划一致
                  .bindText(9, addDays(day, -(i % 365)));      ///< 入库日分布在过去一年
            insert.execute();
            insert.reset();
        }
    }

    // ---------- 阶段 C4：插入 500 条借阅记录 ----------
    {
        Statement insert = db.prepare(
            "INSERT INTO borrow_records(student_id,book_id,borrow_date,due_date,"
            "return_date,status,remark) VALUES(?,?,?,?,?,?,?);");

        // C4-a：200 条当前未还（每 5 条 1 条逾期 → 40 条逾期）
        for (int k = 0; k < currentLoans; ++k) {
            const bool overdue = k % 5 == 0;                  ///< 逾期标记
            const std::string borrow = overdue
                                           ? addDays(day, -(35 + k % 40))   ///< 35..74 天前 → 应还日已过
                                           : addDays(day, -(k % 26));        ///< 0..25 天前 → 应还日在未来
            insert.bindInt(1, pickStudent(k))                 ///< 不超上限的学生
                  .bindInt(2, currentBooks[k])                ///< 互不重复的图书
                  .bindText(3, borrow)
                  .bindText(4, addDays(borrow, 30))           ///< 借期固定 30 天
                  .bindNull(5)                                ///< 未归还 → return_date = NULL
                  .bindInt(6, overdue ? 2 : 0)                ///< 2=逾期，0=借出中
                  .bindText(7, overdue ? "演示：逾期未还" : "演示：当前借阅");
            insert.execute();
            insert.reset();
        }

        // C4-b：300 条历史已归还，只使用未被当前借出的图书
        std::vector<int> freeBooks;                           ///< 可用于历史记录的图书 ID
        for (int id = 1; id <= books; ++id)
            if (!reserved.count(id)) freeBooks.push_back(id);///< 排除当前借出的 200 册
        if (freeBooks.empty())
            throw BusinessException("没有可用于历史借阅的图书");

        for (int j = 0; j < historyLoans; ++j) {
            const std::string borrow = addDays(day, -(40 + j % 150));  ///< 40..189 天前
            insert.bindInt(1, 1 + (j * 3) % students)        ///< 学生轮转
                  .bindInt(2, freeBooks[(j * 7) % freeBooks.size()])  ///< 7 与 800 互质，分散取书
                  .bindText(3, borrow)
                  .bindText(4, addDays(borrow, 30))           ///< 应还日
                  .bindText(5, addDays(borrow, 1 + j % 32))   ///< 归还日 1..32 天后（可略超期）
                  .bindInt(6, 1)                              ///< 已归还
                  .bindText(7, "演示：历史已归还");
            insert.execute();
            insert.reset();
        }
    }

    // ---------- 阶段 D：提交并统计 ----------
    transaction.commit();                                    ///< 提交；此前异常自动回滚

    DemoDataReport report;                                    ///< 统计结果
    Statement stats = db.prepare(                             ///< 一条 SQL 取回 7 个计数
        "SELECT (SELECT COUNT(*) FROM bookshelves),"
        "(SELECT COUNT(*) FROM students),"
        "(SELECT COUNT(*) FROM books),"
        "(SELECT COUNT(*) FROM borrow_records),"
        "(SELECT COUNT(*) FROM books WHERE status=0),"
        "(SELECT COUNT(*) FROM books WHERE status=1),"
        "(SELECT COUNT(*) FROM borrow_records WHERE status=2);");
    if (stats.step()) {
        report.shelves = stats.columnInt(0);                  ///< 书架数
        report.students = stats.columnInt(1);                 ///< 学生数
        report.books = stats.columnInt(2);                    ///< 图书总数
        report.loans = stats.columnInt(3);                    ///< 借阅记录总数
        report.available = stats.columnInt(4);                ///< 在馆数
        report.borrowed = stats.columnInt(5);                 ///< 借出数
        report.overdue = stats.columnInt(6);                  ///< 逾期记录数
    }
    return report;
}

}  // namespace lls
