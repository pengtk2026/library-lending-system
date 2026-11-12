/**
 * @file ui.h
 * @brief 表现层声明：通用工具、模态输入对话框、四个功能面板与主窗口。
 *
 * @par 设计说明
 * - 所属层次：表现层（UI），**唯一包含 FLTK 头文件**的项目头文件；实现见 `src/ui.cpp`。
 *   界面只依赖 `LibraryService`，不得直接访问 DAO 或拼接 SQL。
 * - 控件树：`MainWindow(Fl_Double_Window)` → 顶部统计条 + `Fl_Tabs`（四个 `Fl_Group` 页签）
 *   + 底部状态栏；每个页签含工具栏按钮与一个 `Fl_Hold_Browser` 列表；
 *   三个输入对话框继承 `DialogBase(Fl_Window)`。
 * - FLTK 生命周期：控件由父子关系托管（父窗口销毁时自动删除子控件），代码只 new 不 delete；
 *   `Fl_Group` 构造自动 begin()，构造末尾必须 end() 恢复父级当前组。
 * - 回调约定：所有事件回调为 `static void (Fl_Widget*, void*)`，
 *   注册时把 `this` 作为 void* 传入，回调内 `static_cast` 还原；
 *   `src/ui.cpp` 的 `invoke<Panel, Method>()` 模板为所有回调提供统一异常边界。
 * - 列表约定：`Fl_Hold_Browser` 以 制表符（Tab） 分列、'@' 为格式符；
 *   第 1 行是表头，数据行首列恒为数据库主键，`UiUtils::selectedId()` 据此还原 ID。
 * - 中文显示：由 `src/main.cpp` 在控件创建前把 FL_HELVETICA 四个变体映射到「Microsoft YaHei」。
 *
 * @author 课程设计组
 * @date 2026-11-12
 */

#pragma once

#include "service.h"   // LibraryService：界面层唯一允许调用的下层门面

#include <FL/Fl.H>                  ///< 全局 API：run / wait / set_font / scheme
#include <FL/Fl_Box.H>              ///< 静态文本框（统计条、状态栏、标签）
#include <FL/Fl_Button.H>           ///< 按钮
#include <FL/Fl_Check_Button.H>     ///< 复选框（学生启用状态）
#include <FL/Fl_Choice.H>           ///< 下拉选择框（书架、状态、筛选）
#include <FL/Fl_Double_Window.H>    ///< 双缓冲主窗口基类
#include <FL/Fl_Group.H>            ///< 控件组基类（四个 Panel）
#include <FL/Fl_Hold_Browser.H>     ///< 单选列表浏览器
#include <FL/Fl_Input.H>            ///< 单行文本输入
#include <FL/Fl_Int_Input.H>        ///< 仅允许数字的输入
#include <FL/Fl_Tabs.H>             ///< 页签容器
#include <FL/Fl_Widget.H>           ///< 控件基类与回调类型
#include <FL/Fl_Window.H>           ///< 窗口基类（对话框）
#include <FL/fl_ask.H>              ///< fl_alert / fl_choice / fl_message

#include <string>   ///< std::string：消息与行文本
#include <vector>   ///< std::vector：缓存数据与平行 ID 数组

namespace lls {

class MainWindow;   ///< 前置声明：Panel 需要回指主窗口以写状态栏/触发全局刷新

/**
 * @brief 界面通用工具（全静态类）。
 * @note 集中处理弹窗、控件文本读取、列表配置、格式清洗与「选中行 → 主键」解析，
 *       避免四个 Panel 重复实现相同逻辑。
 */
class UiUtils {
public:
    /** @brief 显示普通信息弹窗。 @param message 信息文本。 */
    static void info(const std::string& message);
    /** @brief 显示错误弹窗（统一「操作失败」前缀）。 @param message 错误原因。 */
    static void error(const std::string& message);
    /**
     * @brief 显示「取消 / 确定」确认框。
     * @param message 提示内容。
     * @return 用户点击「确定」返回 true。
     */
    static bool confirm(const std::string& message);
    /**
     * @brief 安全读取输入控件文本。
     * @param input 输入控件，可为空指针。
     * @return 文本副本；空指针或空值返回空串。
     */
    static std::string text(Fl_Input* input);
    /**
     * @brief 把输入控件内容解析为整数。
     * @param input 数字输入控件。
     * @return 解析结果；空串或非法输入返回 0（交由实体 validate 提示）。
     */
    static int intValue(Fl_Input* input);
    /**
     * @brief 统一配置列表控件。
     * @param browser 目标 Fl_Hold_Browser。
     * @param widths 以 0 结尾的列宽数组；指针须长期有效，故各 Panel 用成员数组保存。
     */
    static void setupBrowser(Fl_Hold_Browser* browser, const int* widths);
    /**
     * @brief 把列表选中行还原为数据库主键。
     * @param browser 列表控件。
     * @return 首列 ID；未选中、选中表头或解析失败返回 0。
     * @note 算法：行号从 1 开始且第 1 行是表头，故 line<=1 无效；
     *       取 text(line) 后按首个 制表符（Tab） 切出首列再 stoi。
     */
    static int selectedId(Fl_Hold_Browser* browser);
    /**
     * @brief 清洗将写入列表的用户文本。
     * @param value 原始文本。
     * @return 可安全显示的文本。
     * @note 制表符/换行替换为空格；'@' 写成 '@@' 转义 FLTK 格式符，防止列错位或格式注入。
     */
    static std::string clean(const std::string& value);
    /**
     * @brief 用制表符把多列文本拼成一行。
     * @param columns 各列文本。
     * @return 以 制表符（Tab） 分隔的行文本。
     */
    static std::string join(const std::vector<std::string>& columns);
};

/**
 * @brief 模态对话框基类（继承 Fl_Window）。
 * @note 算法：`exec()` 中 show() 后进入 `while (shown()) Fl::wait();` 局部事件循环；
 *       按钮回调 hide() 后循环退出，返回 `confirmed_`。取消不产生任何写库动作。
 */
class DialogBase : public Fl_Window {
protected:
    bool confirmed_ = false;   ///< 用户是否按「确定」且校验通过

public:
    /**
     * @brief 构造模态窗口。
     * @param w,h 窗口尺寸。
     * @param title 窗口标题。
     */
    DialogBase(int w, int h, const char* title);
    /** @brief 运行模态事件循环。 @return true 表示确认，false 表示取消。 */
    bool exec();
    /**
     * @brief 「取消」按钮静态回调。
     * @param data 注册时传入的对话框指针。
     */
    static void onCancel(Fl_Widget*, void* data);
};

/**
 * @brief 图书新增/编辑对话框。
 * @note 下拉框用「标签数组 labels_ + 平行 ID 数组 shelfIds_」把选中下标还原为书架外键；
 *       标签先全部构造再 add，避免 vector 扩容导致 c_str() 悬垂。
 */
class BookDialog : public DialogBase {
    Fl_Input* isbn_;                  ///< ISBN 输入框
    Fl_Input* title_;                 ///< 题名输入框
    Fl_Input* author_;                ///< 作者输入框
    Fl_Input* publisher_;             ///< 出版社输入框
    Fl_Input* category_;              ///< 分类输入框
    Fl_Int_Input* year_;              ///< 出版年输入框（仅数字）
    Fl_Choice* shelf_;                ///< 所属书架下拉框
    Fl_Choice* status_;               ///< 状态下拉框（选项序号即枚举值）
    std::vector<int> shelfIds_;       ///< 与下拉项平行的书架 ID
    std::vector<std::string> labels_; ///< 持有下拉标签文本，保证 c_str() 有效
    Book result_;                     ///< 采集结果（编辑时保留原 id）

public:
    /**
     * @brief 构造并回填初始值。
     * @param initial 编辑时的原实体；新增传空 Book（id=0）。
     * @param shelves 可选书架列表。
     */
    BookDialog(const Book& initial, const std::vector<Bookshelf>& shelves);
    /** @brief 取采集结果。 @return Book 常量引用（含有效 id 用于更新）。 */
    const Book& result() const noexcept { return result_; }
    /** @brief 「确定」按钮静态回调。 @param data 对话框指针。 */
    static void onOk(Fl_Widget*, void* data);
    /**
     * @brief 采集控件值、组装实体、调用 validate()；通过则关闭，失败保持对话框打开。
     */
    void collect();
};

/**
 * @brief 学生新增/编辑对话框。
 * @note 新增时默认可借上限 5 且启用；采集失败不关闭窗口，便于用户就地修正。
 */
class StudentDialog : public DialogBase {
    Fl_Input* studentNo_;          ///< 学号输入框
    Fl_Input* name_;               ///< 姓名输入框
    Fl_Input* major_;              ///< 专业班级输入框
    Fl_Input* phone_;              ///< 联系电话输入框
    Fl_Int_Input* maxBorrow_;      ///< 可借上限输入框（仅数字）
    Fl_Check_Button* active_;      ///< 启用状态复选框
    Student result_;               ///< 采集结果（编辑时保留原 id）

public:
    /** @brief 构造并回填。 @param initial 原实体；新增传空 Student。 */
    explicit StudentDialog(const Student& initial);
    /** @brief 取采集结果。 @return Student 常量引用。 */
    const Student& result() const noexcept { return result_; }
    /** @brief 「确定」按钮静态回调。 @param data 对话框指针。 */
    static void onOk(Fl_Widget*, void* data);
    /** @brief 采集并校验学生字段，通过后关闭对话框。 */
    void collect();
};

/**
 * @brief 书架新增/编辑对话框。
 * @note 新增时容量默认 30；容量必须为正整数，由 Bookshelf::validate() 校验。
 */
class ShelfDialog : public DialogBase {
    Fl_Input* code_;               ///< 书架编号输入框
    Fl_Input* location_;           ///< 所在位置输入框
    Fl_Input* category_;           ///< 分类输入框
    Fl_Int_Input* capacity_;       ///< 容量输入框（仅数字）
    Fl_Input* remark_;             ///< 备注输入框
    Bookshelf result_;             ///< 采集结果（编辑时保留原 id）

public:
    /** @brief 构造并回填。 @param initial 原实体；新增传空 Bookshelf。 */
    explicit ShelfDialog(const Bookshelf& initial);
    /** @brief 取采集结果。 @return Bookshelf 常量引用。 */
    const Bookshelf& result() const noexcept { return result_; }
    /** @brief 「确定」按钮静态回调。 @param data 对话框指针。 */
    static void onOk(Fl_Widget*, void* data);
    /** @brief 采集并校验书架字段，通过后关闭对话框。 */
    void collect();
};

/**
 * @brief 图书管理页（Fl_Tabs 的第 1 个页签）。
 * @note `items_` 缓存当前列表数据，编辑时按选中 ID 在缓存中查找原实体，避免再次查库。
 */
class BookPanel : public Fl_Group {
protected:
    LibraryService& svc_;          ///< 业务门面引用
    MainWindow* owner_;            ///< 主窗口指针：写状态栏与触发全局刷新
    Fl_Input* search_;             ///< 关键字检索输入框
    Fl_Hold_Browser* list_;        ///< 图书列表（首列为 ID）
    Fl_Button* btnSearch_;         ///< 查询按钮
    Fl_Button* btnAdd_;            ///< 新增按钮
    Fl_Button* btnEdit_;           ///< 编辑按钮
    Fl_Button* btnDel_;            ///< 删除按钮
    Fl_Button* btnRefresh_;        ///< 刷新按钮
    int widths_[8];                ///< 7 列列宽 + 0 结束（成员数组保证指针长期有效）
    std::vector<Book> items_;      ///< 当前列表数据缓存

public:
    /**
     * @brief 构造图书管理页控件树。
     * @param x,y,w,h 页签区域坐标尺寸。
     * @param svc 业务门面。
     * @param owner 主窗口指针。
     */
    BookPanel(int x, int y, int w, int h, LibraryService& svc, MainWindow* owner);
    /**
     * @brief 重新载入列表。
     * @param keyword 检索关键字，空串表示全部。
     * @note 一次性建「书架 ID → 编号」映射后逐行拼接，避免每行查库。
     */
    void refresh(const std::string& keyword = "");
    /** @brief 把消息转发到主窗口状态栏。 @param message 操作结果。 */
    void status(const std::string& message);
    /** @brief 新增图书流程（对话框 → 写库 → 全局刷新）。 */
    void doAdd();
    /** @brief 编辑图书流程（选中 ID → 缓存原实体 → 对话框 → 更新）。 */
    void doEdit();
    /** @brief 删除图书流程（确认 → 业务引用预检 → 刷新）。 */
    void doDel();
    /** @brief 「查询 / 输入框回车」静态回调。 @param data 面板指针。 */
    static void onSearch(Fl_Widget*, void* data);
    /** @brief 「新增」静态回调。 @param data 面板指针。 */
    static void onAdd(Fl_Widget*, void* data);
    /** @brief 「编辑」静态回调。 @param data 面板指针。 */
    static void onEdit(Fl_Widget*, void* data);
    /** @brief 「删除」静态回调。 @param data 面板指针。 */
    static void onDel(Fl_Widget*, void* data);
    /** @brief 「刷新」静态回调：清空检索条件并重载全部图书。 @param data 面板指针。 */
    static void onRefresh(Fl_Widget*, void* data);
};

/**
 * @brief 学生管理页（Fl_Tabs 的第 2 个页签）。
 * @note 列表额外显示「已借册数」，由 `LibraryService::activeLoanCount()` 逐行查询。
 */
class StudentPanel : public Fl_Group {
protected:
    LibraryService& svc_;          ///< 业务门面引用
    MainWindow* owner_;            ///< 主窗口指针
    Fl_Hold_Browser* list_;        ///< 学生列表（首列为 ID）
    Fl_Button* btnAdd_;            ///< 新增按钮
    Fl_Button* btnEdit_;           ///< 编辑按钮
    Fl_Button* btnDel_;            ///< 删除按钮
    Fl_Button* btnRefresh_;        ///< 刷新按钮
    int widths_[9];                ///< 8 列列宽 + 0 结束
    std::vector<Student> items_;   ///< 当前列表数据缓存

public:
    /** @brief 构造学生管理页控件树。参数含义同 BookPanel。 */
    StudentPanel(int x, int y, int w, int h, LibraryService& svc, MainWindow* owner);
    /** @brief 重载学生列表（含每人当前已借册数）。 */
    void refresh();
    /** @brief 转发消息到状态栏。 @param message 操作结果。 */
    void status(const std::string& message);
    /** @brief 新增学生流程。 */
    void doAdd();
    /** @brief 编辑学生流程。 */
    void doEdit();
    /** @brief 删除学生流程。 */
    void doDel();
    /** @brief 「新增」静态回调。 @param data 面板指针。 */
    static void onAdd(Fl_Widget*, void* data);
    /** @brief 「编辑」静态回调。 @param data 面板指针。 */
    static void onEdit(Fl_Widget*, void* data);
    /** @brief 「删除」静态回调。 @param data 面板指针。 */
    static void onDel(Fl_Widget*, void* data);
    /** @brief 「刷新」静态回调。 @param data 面板指针。 */
    static void onRefresh(Fl_Widget*, void* data);
};

/**
 * @brief 书架管理页（Fl_Tabs 的第 3 个页签）。
 * @note 数据源为 `ShelfStat`（视图聚合结果），界面不做逐架 COUNT。
 */
class ShelfPanel : public Fl_Group {
protected:
    LibraryService& svc_;          ///< 业务门面引用
    MainWindow* owner_;            ///< 主窗口指针
    Fl_Hold_Browser* list_;        ///< 书架列表（首列为 ID）
    Fl_Button* btnAdd_;            ///< 新增按钮
    Fl_Button* btnEdit_;           ///< 编辑按钮
    Fl_Button* btnDel_;            ///< 删除按钮
    Fl_Button* btnRefresh_;        ///< 刷新按钮
    int widths_[8];                ///< 7 列列宽 + 0 结束
    std::vector<ShelfStat> items_; ///< 当前统计列表缓存

public:
    /** @brief 构造书架管理页控件树。参数含义同 BookPanel。 */
    ShelfPanel(int x, int y, int w, int h, LibraryService& svc, MainWindow* owner);
    /** @brief 重载书架列表（藏书数 / 在馆数来自视图）。 */
    void refresh();
    /** @brief 转发消息到状态栏。 @param message 操作结果。 */
    void status(const std::string& message);
    /** @brief 新增书架流程。 */
    void doAdd();
    /** @brief 编辑书架流程（从 ShelfStat 中取出书架本体）。 */
    void doEdit();
    /** @brief 删除书架流程。 */
    void doDel();
    /** @brief 「新增」静态回调。 @param data 面板指针。 */
    static void onAdd(Fl_Widget*, void* data);
    /** @brief 「编辑」静态回调。 @param data 面板指针。 */
    static void onEdit(Fl_Widget*, void* data);
    /** @brief 「删除」静态回调。 @param data 面板指针。 */
    static void onDel(Fl_Widget*, void* data);
    /** @brief 「刷新」静态回调。 @param data 面板指针。 */
    static void onRefresh(Fl_Widget*, void* data);
};

/**
 * @brief 借阅管理页（Fl_Tabs 的第 4 个页签）。
 * @note 关键设计：两个下拉框的「显示标签 / 真实 ID」使用平行数组；
 *       标签统一保存在 `labels_` 中以保证 c_str() 指针稳定；
 *       借书成功后必须全局刷新，防止界面残留已借出的图书。
 */
class LoanPanel : public Fl_Group {
protected:
    LibraryService& svc_;            ///< 业务门面引用
    MainWindow* owner_;              ///< 主窗口指针
    Fl_Choice* studentChoice_;       ///< 学生下拉框
    Fl_Choice* bookChoice_;          ///< 在馆图书下拉框
    Fl_Choice* filterChoice_;        ///< 记录筛选下拉（选项序号即 LoanFilter）
    Fl_Button* btnBorrow_;           ///< 借出按钮
    Fl_Button* btnReturn_;           ///< 还书按钮
    Fl_Button* btnRefresh_;          ///< 刷新按钮
    Fl_Hold_Browser* list_;          ///< 借阅记录列表（首列为记录 ID）
    int widths_[9];                  ///< 8 列列宽 + 0 结束
    std::vector<int> studentIds_;    ///< 与学生下拉项平行的学生 ID
    std::vector<int> bookIds_;       ///< 与图书下拉项平行的图书 ID
    std::vector<int> recordIds_;     ///< 预留：与记录列表平行的记录 ID
    std::vector<std::string> labels_;///< 持有两组下拉标签文本，保证指针稳定
    std::vector<LoanDetail> items_;  ///< 当前借阅明细缓存

public:
    /** @brief 构造借阅管理页控件树。参数含义同 BookPanel。 */
    LoanPanel(int x, int y, int w, int h, LibraryService& svc, MainWindow* owner);
    /**
     * @brief 重建「学生」与「在馆图书」两个下拉框。
     * @note 先收集齐所有标签写入 labels_，再统一 add，避免 vector 扩容使 c_str() 失效。
     */
    void refreshChoices();
    /** @brief 按当前筛选条件重载借阅记录列表。 */
    void refresh();
    /** @brief 转发消息到状态栏。 @param message 操作结果。 */
    void status(const std::string& message);
    /** @brief 借书流程：下拉下标 → 真实 ID → 业务层 → 全局刷新。 */
    void doBorrow();
    /** @brief 还书流程：选中记录 ID → 确认 → 业务层事务 → 全局刷新。 */
    void doReturn();
    /** @brief 「借出」静态回调。 @param data 面板指针。 */
    static void onBorrow(Fl_Widget*, void* data);
    /** @brief 「还书」静态回调。 @param data 面板指针。 */
    static void onReturn(Fl_Widget*, void* data);
    /** @brief 「筛选」下拉静态回调。 @param data 面板指针。 */
    static void onFilter(Fl_Widget*, void* data);
    /** @brief 「刷新」静态回调：同时重建下拉框与列表。 @param data 面板指针。 */
    static void onRefresh(Fl_Widget*, void* data);
};

/**
 * @brief 主窗口：统计条 + 四个页签 + 状态栏，负责全局刷新与组装。
 * @note 构造末尾调用 `refreshAll()` 完成首屏数据装载；
 *       `resizable(tabs_)` 让窗口缩放时由页签区域伸缩。
 */
class MainWindow : public Fl_Double_Window {
    LibraryService& svc_;            ///< 业务门面引用
    Fl_Box* topBar_;                 ///< 顶部统计信息条
    Fl_Box* statusBar_;              ///< 底部状态栏
    Fl_Button* btnRefresh_;          ///< 「刷新全部」按钮
    Fl_Button* btnAbout_;            ///< 「关于」按钮
    Fl_Tabs* tabs_;                  ///< 四个页签容器
    BookPanel* bookPanel_;           ///< 图书管理页
    StudentPanel* studentPanel_;     ///< 学生管理页
    ShelfPanel* shelfPanel_;         ///< 书架管理页
    LoanPanel* loanPanel_;           ///< 借阅管理页

public:
    /**
     * @brief 构造主窗口并组装控件树。
     * @param w,h 窗口尺寸（1024x680）。
     * @param title 窗口标题（UTF-8）。
     * @param svc 业务门面。
     */
    MainWindow(int w, int h, const char* title, LibraryService& svc);
    /**
     * @brief 更新底部状态栏。
     * @param message 操作结果或错误原因。
     * @note 用 copy_label 复制字符串，可安全传入临时拼接结果。
     */
    void setStatus(const std::string& message);
    /**
     * @brief 全局刷新：逾期维护 → 统计条 → 四个列表 → 借阅下拉框与记录列表。
     * @note 全流程 try/catch，任一步失败只在状态栏提示，不让窗口崩溃或半刷新。
     */
    void refreshAll();
    /** @brief 「刷新全部」静态回调。 @param data 主窗口指针。 */
    static void onRefresh(Fl_Widget*, void* data);
    /** @brief 「关于」静态回调：显示版本与技术栈信息。 */
    static void onAbout(Fl_Widget*, void* data);
};

}  // namespace lls
