/**
 * @file ui.cpp
 * @brief 表现层实现：FLTK 主窗口、四个功能面板、三个模态输入对话框与全部事件回调。
 *
 * @par 设计说明
 * - 所属层次：表现层（UI），是唯一包含 FLTK 头文件的实现文件；
 *   对应声明 include/ui.h。界面只调用 LibraryService，绝不直接访问 DAO 或拼接 SQL。
 * - 控件树：MainWindow(Fl_Double_Window 1024x680)
 *   ├─ 顶部统计条 Fl_Box + 「刷新全部」「关于」按钮
 *   ├─ Fl_Tabs（四个 Fl_Group 页签：BookPanel / StudentPanel / ShelfPanel / LoanPanel）
 *   └─ 底部状态栏 Fl_Box
 *   每个页签内含工具栏按钮与一个 Fl_Hold_Browser 列表；输入对话框继承 DialogBase(Fl_Window)。
 * - FLTK 生命周期：控件由父子关系托管（父窗口销毁时自动删除子控件），
 *   因此代码只 new 不 delete；Fl_Group 构造会自动 begin()，构造末尾必须 end() 恢复父级，
 *   MainWindow 最后 tabs_->end() → end() 完成整棵控件树。
 * - 事件模型：所有回调为 `static void (Fl_Widget*, void*)`，用 static_cast 从 void* 取回对象指针；
 *   invoke<Panel, &Panel::method>() 模板把「异常边界」收敛到一处，
 *   保证任何 AppException / std::exception 都被转换为对话框 + 状态栏提示，绝不穿透 FLTK 回调。
 * - 列表渲染算法：Fl_Hold_Browser 以 制表符（Tab） 为列分隔符、'@' 为格式符；
 *   第 1 行固定为表头（用 @b 加粗），数据行首列恒为主键 ID，
 *   UiUtils::selectedId() 通过解析首列把「用户选中的行」还原为数据库 ID。
 *   用户数据中的 @ 需转义为 @@，制表符/换行需替换为空格，防止列错位或格式注入。
 * - 模态对话框算法：DialogBase::exec() 调用 show() 后进入
 *   `while (shown()) Fl::wait();` 的局部事件循环，确定按钮先校验再 hide()，
 *   取消按钮直接 hide()；exec() 返回 confirmed_，取消不产生任何写库动作。
 * - 中文显示：main.cpp 中通过 Fl::set_font 指定「Microsoft YaHei」；
 *   本文件所有文案与数据库文本均为 UTF-8。
 *
 * @author 课程设计组
 * @date 2026-11-12
 */

#include "ui.h"

#include <map>        // std::map：书架 ID → 编号 的显示映射
#include <sstream>    // 预留：与统计/报告一致的流式格式化能力
#include <string>     // std::string：控件文本与行拼接
#include <vector>     // std::vector：下拉框标签与平行 ID 数组

namespace lls {
namespace {  // 匿名命名空间：仅本文件使用的布局/回调辅助工具

/*
 * @brief 在对话框中创建「左侧文字标签 + 右侧单行输入框」的标准行。
 * @param y 行的纵坐标（对话框相对坐标）。
 * @param label 左侧标签文字。
 * @param width 输入框宽度，默认 290。
 * @return 新创建的 Fl_Input 指针（由父窗口托管生命周期）。
 * @note 算法：FLTK 的 label 默认画在控件左侧外部，容易被裁剪；
 *       故用独立 Fl_Box + FL_ALIGN_LEFT|FL_ALIGN_INSIDE 承载标签，输入框自身不带 label。
 */
Fl_Input* makeInput(int y, const char* label, int width = 290) {
    Fl_Box* caption = new Fl_Box(20, y, 110, 26, label);     ///< 左侧静态标签
    caption->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);         ///< 文字在框内左对齐
    return new Fl_Input(140, y, width, 26);                   ///< 右侧单行文本输入框
}

/* @brief 创建「标签 + 只允许数字的输入框」行，用于出版年、可借上限、容量。 */
Fl_Int_Input* makeIntInput(int y, const char* label) {
    Fl_Box* caption = new Fl_Box(20, y, 110, 26, label);     ///< 左侧静态标签
    caption->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);         ///< 框内左对齐
    return new Fl_Int_Input(140, y, 290, 26);                 ///< 数字输入框
}

/* @brief 创建「标签 + 下拉选择框」行，用于所属书架、状态、筛选。 */
Fl_Choice* makeChoice(int y, const char* label) {
    Fl_Box* caption = new Fl_Box(20, y, 110, 26, label);     ///< 左侧静态标签
    caption->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);         ///< 框内左对齐
    return new Fl_Choice(140, y, 290, 26);                    ///< 下拉选择框
}

/*
 * @brief FLTK 回调的统一异常边界（函数模板）。
 * @tparam Panel 面板类型。
 * @tparam Method 指向「无参成员动作」的成员函数指针（非类型模板参数）。
 * @param data 注册 callback 时传入的面板对象指针。
 * @note 主要算法：static_cast 还原对象 → 调用成员动作 →
 *       捕获 AppException 与 std::exception 两类异常 → 弹窗提示 + 写状态栏。
 *       用模板把 20 多个按钮回调的重复 try/catch 收敛成一处，避免异常穿透到 FLTK 事件循环。
 */
template <typename Panel, void (Panel::*Method)()>
void invoke(Fl_Widget*, void* data) {
    Panel* self = static_cast<Panel*>(data);                  ///< 还原面板对象
    try {
        (self->*Method)();                                   ///< 执行具体业务动作
    } catch (const AppException& error) {                    ///< 业务/校验/数据库异常
        UiUtils::error(error.what());                        ///< 弹窗告知用户
        self->status(error.what());                          ///< 同步写入底部状态栏
    } catch (const std::exception& error) {                  ///< 其它标准异常兜底
        UiUtils::error(error.what());
        self->status(error.what());
    }
}

/*
 * @brief 列表单元格的空值占位与格式清洗。
 * @param value 原始文本。
 * @return 非空时返回清洗后的文本，空时返回 "-"。
 */
std::string statusOr(const std::string& value) {
    return UiUtils::clean(value.empty() ? std::string("-") : value);  ///< 空字段显示短横线
}

}  // namespace 匿名命名空间结束

// ==================== UiUtils（界面通用工具） ====================

/* @brief 显示普通信息弹窗。 */
void UiUtils::info(const std::string& message) {
    fl_alert("%s", message.c_str());                         ///< %s 传参避免文本中的 % 被误解析
}

/* @brief 显示错误弹窗，统一加「操作失败」前缀。 */
void UiUtils::error(const std::string& message) {
    fl_alert("操作失败：\n%s", message.c_str());
}

/*
 * @brief 显示「取消 / 确定」确认框。
 * @param message 提示内容。
 * @return 用户点击「确定」返回 true。
 * @note fl_choice 的第 2、3 参数是按钮标签，返回值为按钮序号（0=取消，1=确定）。
 */
bool UiUtils::confirm(const std::string& message) {
    return fl_choice("%s", "取消", "确定",
                     static_cast<const char*>(nullptr),       ///< 第三个按钮不显示
                     message.c_str()) == 1;
}

/* @brief 安全读取 Fl_Input 的文本，空指针返回空串。 */
std::string UiUtils::text(Fl_Input* input) {
    const char* value = input ? input->value() : nullptr;    ///< FLTK 内部字符串指针
    return value ? std::string(value) : std::string();       ///< 复制为值语义字符串
}

/*
 * @brief 把输入控件内容解析为整数。
 * @param input 数字输入框。
 * @return 解析结果；空串或非法输入返回 0（交由实体 validate 给出具体提示）。
 */
int UiUtils::intValue(Fl_Input* input) {
    std::string value = trim(text(input));                    ///< 去除首尾空白
    if (value.empty()) return 0;                              ///< 未填写 → 0
    try {
        return std::stoi(value);                              ///< 标准库解析
    } catch (...) {
        return 0;                                             ///< 非法输入不在此抛异常
    }
}

/*
 * @brief 清洗将写入 Fl_Browser 的用户文本。
 * @param value 原始文本。
 * @return 可安全显示的文本。
 * @note 主要算法：制表符/换行会破坏列分隔，统一替换为空格；
 *       '@' 是 FLTK 行内格式符（如 @b 粗体），必须写成 '@@' 转义为字面量，
 *       否则书名中的 @ 会导致后续字符被误解析为格式指令。
 */
std::string UiUtils::clean(const std::string& value) {
    std::string result;                                       ///< 清洗后的结果
    for (char ch : value) {                                   ///< 逐字符扫描，O(n)
        if (ch == '\t' || ch == '\n' || ch == '\r')
            result += ' ';                                    ///< 控制字符 → 空格
        else if (ch == '@')
            result += "@@";                                   ///< 格式符转义
        else
            result += ch;                                     ///< 普通字符保留
    }
    return result;
}

/*
 * @brief 把一列文本用制表符拼成 Fl_Browser 的一行。
 * @param columns 各列文本。
 * @return 以 制表符（Tab） 分隔的行文本。
 */
std::string UiUtils::join(const std::vector<std::string>& columns) {
    std::string result;                                       ///< 结果行
    for (std::size_t i = 0; i < columns.size(); ++i) {        ///< 逐列追加
        if (i) result += '\t';                                ///< 列间分隔符
        result += columns[i];
    }
    return result;
}

/*
 * @brief 统一配置列表控件。
 * @param browser 目标 Fl_Hold_Browser。
 * @param widths 以 0 结尾的列宽数组（指针必须在控件生命周期内有效，故各 Panel 用成员数组保存）。
 * @note 算法：column_widths 固定列宽、column_char(Tab 字符) 指定列分隔符、
 *       保留 '@' 为格式符（表头 @b 加粗）、textsize 与 box 统一外观。
 */
void UiUtils::setupBrowser(Fl_Hold_Browser* browser, const int* widths) {
    browser->column_widths(widths);                          ///< 列宽表
    browser->column_char('\t');                              ///< 制表符分列
    browser->textsize(13);                                   ///< 正文字号
    browser->box(FL_DOWN_BOX);                               ///< 凹陷边框
}

/*
 * @brief 把「列表选中行」还原为数据库主键。
 * @param browser 列表控件。
 * @return 首列 ID；未选中/选中表头/解析失败返回 0。
 * @note 主要算法：Fl_Browser 行号从 1 开始，第 1 行恒为表头，故 line<=1 视为无效；
 *       取 text(line) 后按首个 制表符（Tab） 切出首列，再 stoi 转整数。
 *       各面板约定「首列必须是 ID」，这是选中行到数据库记录的关键映射约定。
 */
int UiUtils::selectedId(Fl_Hold_Browser* browser) {
    int line = browser->value();                              ///< 当前选中行号（1-based）
    if (line <= 1) return 0;                                 ///< 0=未选中，1=表头
    const char* raw = browser->text(line);                   ///< 选中行原始文本
    if (!raw) return 0;
    std::string row(raw);                                    ///< 转为可切分的字符串
    std::size_t tab = row.find('\t');                        ///< 第一个列分隔符
    std::string first = tab == std::string::npos
                            ? row : row.substr(0, tab);      ///< 首列文本
    try {
        return std::stoi(first);                              ///< 首列即主键
    } catch (...) {
        return 0;                                             ///< 表头等非法情形
    }
}

// ==================== DialogBase（模态对话框基类） ====================

/* @brief 构造模态窗口；set_modal() 使其显示期间阻塞父窗口输入。 */
DialogBase::DialogBase(int w, int h, const char* title)
    : Fl_Window(w, h, title) {
    set_modal();
}

/*
 * @brief 运行模态事件循环。
 * @return true 表示用户按「确定」且校验通过；false 表示取消/关闭。
 * @note 主要算法：show() 之后用 `while (shown()) Fl::wait();` 持续派发事件，
 *       按钮回调 hide() 后 shown() 变假，循环退出；这是 FLTK 无内置 exec() 时的标准模态实现。
 */
bool DialogBase::exec() {
    show();
    while (shown()) Fl::wait();                               ///< 局部事件循环
    return confirmed_;                                       ///< 返回用户是否确认
}

/*
 * @brief 「取消」按钮回调（静态函数，供 FLTK 调用）。
 * @param data 注册时传入的对话框指针。
 */
void DialogBase::onCancel(Fl_Widget*, void* data) {
    DialogBase* self = static_cast<DialogBase*>(data);        ///< 还原对话框对象
    self->confirmed_ = false;                                ///< 标记为取消
    self->hide();                                             ///< 关闭窗口，结束模态循环
}

// ==================== BookDialog（图书输入对话框） ====================

/*
 * @brief 构造新增/编辑图书对话框并回填初始值。
 * @param initial 编辑时的原实体；新增时传空 Book（id=0）。
 * @param shelves 可选书架列表（供「所属书架」下拉框）。
 * @note 算法：先建 8 行输入控件 → 用 initial 回填 → 构造书架标签并选中原书架 →
 *       填入 4 个状态选项 → 「确定/取消」按钮 → end() 结束控件树。
 *       标签先全部 push 到 labels_ 再 add，避免 vector 扩容导致 c_str() 指针失效。
 */
BookDialog::BookDialog(const Book& initial,
                       const std::vector<Bookshelf>& shelves)
    : DialogBase(460, 430, initial.id() > 0 ? "编辑图书" : "新增图书"),
      result_(initial) {                                     ///< 保留 id，编辑时用于 UPDATE
    isbn_ = makeInput(20, "ISBN");                           ///< 行 1：ISBN
    title_ = makeInput(56, "题名");                          ///< 行 2：题名
    author_ = makeInput(92, "作者");                         ///< 行 3：作者
    publisher_ = makeInput(128, "出版社");                   ///< 行 4：出版社
    year_ = makeIntInput(164, "出版年");                     ///< 行 5：出版年（数字）
    category_ = makeInput(200, "分类");                      ///< 行 6：分类
    shelf_ = makeChoice(236, "所属书架");                    ///< 行 7：书架下拉
    status_ = makeChoice(272, "状态");                       ///< 行 8：状态下拉

    isbn_->value(result_.isbn().c_str());                    ///< 回填 ISBN
    title_->value(result_.title().c_str());                  ///< 回填题名
    author_->value(result_.author().c_str());                ///< 回填作者
    publisher_->value(result_.publisher().c_str());          ///< 回填出版社
    category_->value(result_.category().c_str());            ///< 回填分类
    year_->value(result_.publishYear() > 0                   ///< 0 表示未填，显示空
                     ? std::to_string(result_.publishYear()).c_str()
                     : "");

    for (const Bookshelf& shelf : shelves) {                 ///< 构造「编号 位置」标签
        labels_.push_back(shelf.code() + "  " + shelf.location());  ///< 显示文本（成员持有，保证指针有效）
        shelfIds_.push_back(shelf.id());                     ///< 与标签平行的 ID 数组
    }
    for (const std::string& label : labels_)
        shelf_->add(label.c_str());                          ///< 逐项加入下拉框
    for (int i = 0; i < static_cast<int>(shelfIds_.size()); ++i) {
        if (shelfIds_[i] == result_.shelfId())
            shelf_->value(i);                                ///< 编辑时选中原书架
    }
    if (shelfIds_.empty())
        shelf_->add("（请先创建书架）");                      ///< 无书架时的占位提示

    status_->add("在馆");                                     ///< 选项 0
    status_->add("借出");                                     ///< 选项 1
    status_->add("遗失");                                     ///< 选项 2
    status_->add("维修");                                     ///< 选项 3（与枚举顺序一致）
    status_->value(static_cast<int>(result_.status()));       ///< 回填状态

    Fl_Button* ok = new Fl_Button(240, 370, 90, 32, "确定");///< 确定按钮
    ok->callback(onOk, this);                                ///< 注册确定回调
    Fl_Button* cancel = new Fl_Button(340, 370, 90, 32, "取消");  ///< 取消按钮
    cancel->callback(DialogBase::onCancel, this);            ///< 复用基类取消逻辑
    end();                                                   ///< 结束控件树
}

/* @brief 「确定」回调：委托给成员 collect() 完成采集与校验。 */
void BookDialog::onOk(Fl_Widget*, void* data) {
    static_cast<BookDialog*>(data)->collect();
}

/*
 * @brief 采集控件值 → 组装 Book → 调用实体 validate() → 通过则关闭对话框。
 * @note 主要算法：先逐项读入 result_；书架下拉通过「下标 → shelfIds_」平行数组还原外键；
 *       校验失败时弹窗且**不 hide()**，用户可留在对话框内修正，避免重复打开。
 */
void BookDialog::collect() {
    result_.setIsbn(UiUtils::text(isbn_));                   ///< ISBN
    result_.setTitle(UiUtils::text(title_));                 ///< 题名
    result_.setAuthor(UiUtils::text(author_));               ///< 作者
    result_.setPublisher(UiUtils::text(publisher_));         ///< 出版社
    result_.setCategory(UiUtils::text(category_));           ///< 分类
    result_.setPublishYear(UiUtils::intValue(year_));        ///< 出版年
    int index = shelf_->value();                             ///< 选中的书架下标
    if (index >= 0 && index < static_cast<int>(shelfIds_.size()))
        result_.setShelfId(shelfIds_[index]);                ///< 下标 → 真实 ID
    result_.setStatus(Book::statusFromInt(status_->value()));///< 选项序号即枚举值
    const std::string error = result_.validate();            ///< 领域层不变式校验
    if (!error.empty()) {
        UiUtils::error(error);                               ///< 显示具体字段错误
        return;                                              ///< 保持对话框打开
    }
    confirmed_ = true;                                       ///< 标记确认
    hide();                                                  ///< 结束模态循环
}

// ==================== StudentDialog（学生输入对话框） ====================

/* @brief 构造新增/编辑学生对话框并回填初始值；新增时默认上限 5 且启用。 */
StudentDialog::StudentDialog(const Student& initial)
    : DialogBase(460, 340, initial.id() > 0 ? "编辑学生" : "新增学生"),
      result_(initial) {                                     ///< 保留 id 供更新
    studentNo_ = makeInput(20, "学号");                       ///< 行 1：学号
    name_ = makeInput(56, "姓名");                            ///< 行 2：姓名
    major_ = makeInput(92, "专业班级");                       ///< 行 3：专业
    phone_ = makeInput(128, "联系电话");                      ///< 行 4：电话
    maxBorrow_ = makeIntInput(164, "可借上限");               ///< 行 5：上限（数字）
    active_ = new Fl_Check_Button(140, 200, 220, 26,         ///< 行 6：启用复选框
                                  "启用（允许借书）");

    studentNo_->value(result_.studentNo().c_str());          ///< 回填学号
    name_->value(result_.name().c_str());                    ///< 回填姓名
    major_->value(result_.major().c_str());                  ///< 回填专业
    phone_->value(result_.phone().c_str());                  ///< 回填电话
    if (result_.id() == 0)
        result_.setMaxBorrow(Student::defaultMaxBorrow());   ///< 新增时给默认上限 5
    maxBorrow_->value(std::to_string(result_.maxBorrow()).c_str());  ///< 回填上限
    active_->value(result_.active() ? 1 : 0);                ///< 回填启用状态

    Fl_Button* ok = new Fl_Button(240, 280, 90, 32, "确定");///< 确定按钮
    ok->callback(onOk, this);
    Fl_Button* cancel = new Fl_Button(340, 280, 90, 32, "取消");  ///< 取消按钮
    cancel->callback(DialogBase::onCancel, this);
    end();                                                   ///< 结束控件树
}

/* @brief 「确定」回调：委托 collect()。 */
void StudentDialog::onOk(Fl_Widget*, void* data) {
    static_cast<StudentDialog*>(data)->collect();
}

/* @brief 采集学生字段并校验；失败保持对话框打开。 */
void StudentDialog::collect() {
    result_.setStudentNo(UiUtils::text(studentNo_));         ///< 学号
    result_.setName(UiUtils::text(name_));                   ///< 姓名
    result_.setMajor(UiUtils::text(major_));                 ///< 专业
    result_.setPhone(UiUtils::text(phone_));                 ///< 电话
    result_.setMaxBorrow(UiUtils::intValue(maxBorrow_));     ///< 可借上限
    result_.setActive(active_->value() != 0);                ///< 复选框 → bool
    const std::string error = result_.validate();            ///< 不变式校验
    if (!error.empty()) {
        UiUtils::error(error);
        return;
    }
    confirmed_ = true;
    hide();
}

// ==================== ShelfDialog（书架输入对话框） ====================

/* @brief 构造新增/编辑书架对话框；新增时容量默认 30。 */
ShelfDialog::ShelfDialog(const Bookshelf& initial)
    : DialogBase(460, 340, initial.id() > 0 ? "编辑书架" : "新增书架"),
      result_(initial) {                                     ///< 保留 id 供更新
    code_ = makeInput(20, "书架编号");                        ///< 行 1：编号
    location_ = makeInput(56, "所在位置");                    ///< 行 2：位置
    category_ = makeInput(92, "分类");                        ///< 行 3：分类
    capacity_ = makeIntInput(128, "容量（册）");              ///< 行 4：容量（数字）
    remark_ = makeInput(164, "备注");                         ///< 行 5：备注

    code_->value(result_.code().c_str());                    ///< 回填编号
    location_->value(result_.location().c_str());            ///< 回填位置
    category_->value(result_.category().c_str());            ///< 回填分类
    remark_->value(result_.remark().c_str());                ///< 回填备注
    capacity_->value(result_.capacity() > 0
                         ? std::to_string(result_.capacity()).c_str()
                         : "30");                            ///< 新增时默认 30 册

    Fl_Button* ok = new Fl_Button(240, 280, 90, 32, "确定");///< 确定按钮
    ok->callback(onOk, this);
    Fl_Button* cancel = new Fl_Button(340, 280, 90, 32, "取消");  ///< 取消按钮
    cancel->callback(DialogBase::onCancel, this);
    end();                                                   ///< 结束控件树
}

/* @brief 「确定」回调：委托 collect()。 */
void ShelfDialog::onOk(Fl_Widget*, void* data) {
    static_cast<ShelfDialog*>(data)->collect();
}

/* @brief 采集书架字段并校验；失败保持对话框打开。 */
void ShelfDialog::collect() {
    result_.setCode(UiUtils::text(code_));                   ///< 编号
    result_.setLocation(UiUtils::text(location_));           ///< 位置
    result_.setCategory(UiUtils::text(category_));           ///< 分类
    result_.setRemark(UiUtils::text(remark_));               ///< 备注
    result_.setCapacity(UiUtils::intValue(capacity_));       ///< 容量
    const std::string error = result_.validate();            ///< 不变式校验
    if (!error.empty()) {
        UiUtils::error(error);
        return;
    }
    confirmed_ = true;
    hide();
}

// ==================== BookPanel（图书管理页） ====================

/*
 * @brief 构造图书管理页控件树。
 * @param x,y,w,h Fl_Group 区域（也是 Fl_Tabs 页签区域）。
 * @param svc 业务门面引用。
 * @param owner 主窗口指针，用于写状态栏与触发全局刷新。
 * @note 算法：子控件坐标 = 组的 (x,y) + 偏移；widths_ 是成员数组，
 *       因为 Fl_Browser::column_widths 只保存指针，局部数组会导致悬垂。
 */
BookPanel::BookPanel(int x, int y, int w, int h,
                     LibraryService& svc, MainWindow* owner)
    : Fl_Group(x, y, w, h, "图书管理"),                      ///< 页签标题
      svc_(svc), owner_(owner) {
    const int columns[] = {60, 150, 260, 150, 110, 90, 90, 0};  ///< 7 列宽度 + 0 结束
    for (int i = 0; i < 8; ++i) widths_[i] = columns[i];    ///< 复制到成员，延长生命周期

    search_ = new Fl_Input(x + 20, y + 20, 260, 28);         ///< 关键字输入框
    search_->callback(onSearch, this);                       ///< 回车也可触发检索
    btnSearch_ = new Fl_Button(x + 290, y + 20, 80, 28, "查询");    ///< 查询按钮
    btnSearch_->callback(onSearch, this);
    btnAdd_ = new Fl_Button(x + 390, y + 20, 80, 28, "新增");       ///< 新增按钮
    btnAdd_->callback(onAdd, this);
    btnEdit_ = new Fl_Button(x + 480, y + 20, 80, 28, "编辑");      ///< 编辑按钮
    btnEdit_->callback(onEdit, this);
    btnDel_ = new Fl_Button(x + 570, y + 20, 80, 28, "删除");       ///< 删除按钮
    btnDel_->callback(onDel, this);
    btnRefresh_ = new Fl_Button(x + 660, y + 20, 80, 28, "刷新");   ///< 刷新按钮
    btnRefresh_->callback(onRefresh, this);

    list_ = new Fl_Hold_Browser(x + 20, y + 65, w - 40, h - 85);     ///< 图书列表
    UiUtils::setupBrowser(list_, widths_);                   ///< 统一列宽/分隔符/外观
    end();                                                   ///< 结束本组控件树
}

/*
 * @brief 重新载入图书列表。
 * @param keyword 检索关键字，空串表示全部。
 * @note 主要算法：
 *       1) 调 Service 取回 vector<Book>；
 *       2) 一次性取回书架列表，建 `id → code` 的 map（避免每行查库造成 N+1）；
 *       3) 清空列表 → 插入 @b 加粗表头 → 逐行拼接「ID 必须为第一列」的文本；
 *       4) 所有用户文本经 clean() 转义，空字段经 statusOr() 显示 "-"。
 */
void BookPanel::refresh(const std::string& keyword) {
    items_ = svc_.listBooks(keyword);                         ///< 缓存当前列表数据（编辑时按 ID 查找）
    std::map<int, std::string> shelfCodes;                    ///< 书架 ID → 显示编号
    for (const Bookshelf& shelf : svc_.listShelves())
        shelfCodes[shelf.id()] = shelf.code();
    list_->clear();                                           ///< 清空旧行
    list_->add("@b编号\t@bISBN\t@b题名\t@b作者\t@b书架\t@b状态\t@b出版年");  ///< 表头
    for (const Book& book : items_) {
        std::string code = shelfCodes.count(book.shelfId())   ///< 优先显示书架编号
                               ? shelfCodes[book.shelfId()]
                               : std::to_string(book.shelfId());  ///< 找不到时退回 ID
        std::string row = UiUtils::join({                     ///< 拼装 7 列
            std::to_string(book.id()),                       ///< 首列：主键（选中解析依赖）
            UiUtils::clean(book.isbn()),
            UiUtils::clean(book.title()),
            statusOr(book.author()),
            UiUtils::clean(code),
            book.statusText(),
            book.publishYear() > 0
                ? std::to_string(book.publishYear()) : "-"});
        list_->add(row.c_str());                             ///< Fl_Browser 会复制文本
    }
}

/* @brief 把消息转发到主窗口状态栏。 */
void BookPanel::status(const std::string& message) {
    if (owner_) owner_->setStatus(message);
}

/*
 * @brief 新增图书流程：打开对话框 → 取消则直接返回 → 确认后写库 → 全局刷新。
 * @note 用花括号构造对话框避免 C++ 的 most vexing parse（临时对象被误判为函数声明）。
 */
void BookPanel::doAdd() {
    BookDialog dialog{Book{}, svc_.listShelves()};           ///< 模态输入对话框
    if (!dialog.exec()) return;                              ///< 用户取消：不写库
    svc_.addBook(dialog.result());                           ///< 写库（异常由 invoke 捕获）
    if (owner_) owner_->refreshAll();                        ///< 同步统计条与其它页签
    status("新增图书成功");
}

/*
 * @brief 编辑图书流程：解析选中 ID → 从缓存找原实体 → 对话框 → 更新。
 * @exception NotFoundException 选中行已不在缓存中（列表过期）。
 */
void BookPanel::doEdit() {
    const int id = UiUtils::selectedId(list_);               ///< 选中行首列 ID
    if (id <= 0) {
        status("请先在列表中选择一本图书");
        return;
    }
    Book current;                                            ///< 待编辑的原实体
    bool found = false;                                      ///< 是否在缓存中找到
    for (const Book& book : items_) {
        if (book.id() == id) {                               ///< 线性查找（列表规模千级，开销可忽略）
            current = book;
            found = true;
            break;
        }
    }
    if (!found) throw NotFoundException("图书不在当前列表中，请刷新后重试");
    BookDialog dialog(current, svc_.listShelves());          ///< 回填原值
    if (!dialog.exec()) return;
    svc_.updateBook(dialog.result());                        ///< 对话框保留原 id
    if (owner_) owner_->refreshAll();
    status("图书信息已更新");
}

/* @brief 删除图书流程：选中 → 二次确认 → 业务层引用预检 → 全局刷新。 */
void BookPanel::doDel() {
    const int id = UiUtils::selectedId(list_);               ///< 选中行 ID
    if (id <= 0) {
        status("请先在列表中选择一本图书");
        return;
    }
    if (!UiUtils::confirm("确定删除选中的图书吗？")) return;  ///< 防误删
    svc_.removeBook(id);                                     ///< 业务层会检查未归还记录
    if (owner_) owner_->refreshAll();
    status("图书已删除");
}

/* @brief 「查询 / 输入框回车」回调：按关键字重绘列表。 */
void BookPanel::onSearch(Fl_Widget*, void* data) {
    BookPanel* self = static_cast<BookPanel*>(data);          ///< 还原面板
    try {
        self->refresh(UiUtils::text(self->search_));          ///< 按关键字刷新
        self->status("检索完成");
    } catch (const std::exception& error) {
        UiUtils::error(error.what());                         ///< 异常边界
    }
}

/* @brief 「新增」按钮回调，统一走 invoke 异常边界。 */
void BookPanel::onAdd(Fl_Widget*, void* data) { invoke<BookPanel, &BookPanel::doAdd>(nullptr, data); }
/* @brief 「编辑」按钮回调。 */
void BookPanel::onEdit(Fl_Widget*, void* data) { invoke<BookPanel, &BookPanel::doEdit>(nullptr, data); }
/* @brief 「删除」按钮回调。 */
void BookPanel::onDel(Fl_Widget*, void* data) { invoke<BookPanel, &BookPanel::doDel>(nullptr, data); }

/*
 * @brief 「刷新」按钮回调：清空检索条件并重载全部图书。
 * @note 不能直接用 invoke 模板，因为 refresh 带默认参数、成员函数指针签名不是 void()，
 *       故单独实现同样的 try/catch 边界。
 */
void BookPanel::onRefresh(Fl_Widget*, void* data) {
    BookPanel* self = static_cast<BookPanel*>(data);          ///< 还原面板
    try {
        self->search_->value("");                            ///< 清空检索框
        self->refresh();                                     ///< 全量重载
        self->status("图书列表已刷新");
    } catch (const std::exception& error) {
        UiUtils::error(error.what());
    }
}

// ==================== StudentPanel（学生管理页） ====================

/* @brief 构造学生管理页：4 个按钮 + 8 列列表。 */
StudentPanel::StudentPanel(int x, int y, int w, int h,
                           LibraryService& svc, MainWindow* owner)
    : Fl_Group(x, y, w, h, "学生管理"), svc_(svc), owner_(owner) {
    const int columns[] = {60, 120, 110, 200, 130, 100, 90, 100, 0};  ///< 8 列宽 + 0 结束
    for (int i = 0; i < 9; ++i) widths_[i] = columns[i];    ///< 复制到成员
    btnAdd_ = new Fl_Button(x + 20, y + 20, 80, 28, "新增");///< 新增按钮
    btnAdd_->callback(onAdd, this);
    btnEdit_ = new Fl_Button(x + 110, y + 20, 80, 28, "编辑");  ///< 编辑按钮
    btnEdit_->callback(onEdit, this);
    btnDel_ = new Fl_Button(x + 200, y + 20, 80, 28, "删除");   ///< 删除按钮
    btnDel_->callback(onDel, this);
    btnRefresh_ = new Fl_Button(x + 290, y + 20, 80, 28, "刷新");  ///< 刷新按钮
    btnRefresh_->callback(onRefresh, this);
    list_ = new Fl_Hold_Browser(x + 20, y + 65, w - 40, h - 85);     ///< 学生列表
    UiUtils::setupBrowser(list_, widths_);
    end();
}

/*
 * @brief 重载学生列表，额外显示每人当前已借册数。
 * @note 算法：逐行调用 Service::activeLoanCount()（每个学生一次 COUNT 查询）；
 *       学生规模百级，索引 idx_loans_student 保证开销很小。
 */
void StudentPanel::refresh() {
    items_ = svc_.listStudents();                             ///< 缓存当前学生列表
    list_->clear();
    list_->add("@b编号\t@b学号\t@b姓名\t@b专业班级\t@b电话\t@b上限\t@b状态\t@b已借");
    for (const Student& student : items_) {
        const int borrowed = svc_.activeLoanCount(student.id());  ///< 当前未归还册数
        std::string row = UiUtils::join({
            std::to_string(student.id()),                     ///< 首列：主键
            UiUtils::clean(student.studentNo()),
            UiUtils::clean(student.name()),
            statusOr(student.major()),
            statusOr(student.phone()),
            std::to_string(student.maxBorrow()),
            student.active() ? "启用" : "停用",
            std::to_string(borrowed)});
        list_->add(row.c_str());
    }
}

/* @brief 转发消息到主窗口状态栏。 */
void StudentPanel::status(const std::string& message) {
    if (owner_) owner_->setStatus(message);
}

/* @brief 新增学生：模态对话框 → 写库 → 全局刷新。 */
void StudentPanel::doAdd() {
    StudentDialog dialog{Student{}};                          ///< 花括号构造避免 vexing parse
    if (!dialog.exec()) return;
    svc_.addStudent(dialog.result());
    if (owner_) owner_->refreshAll();
    status("新增学生成功");
}

/* @brief 编辑学生：从缓存取原实体 → 对话框 → 更新。 */
void StudentPanel::doEdit() {
    const int id = UiUtils::selectedId(list_);               ///< 选中行 ID
    if (id <= 0) {
        status("请先在列表中选择一名学生");
        return;
    }
    Student current;                                          ///< 原实体
    bool found = false;                                      ///< 是否命中缓存
    for (const Student& student : items_) {
        if (student.id() == id) {
            current = student;
            found = true;
            break;
        }
    }
    if (!found) throw NotFoundException("学生不在当前列表中，请刷新后重试");
    StudentDialog dialog(current);
    if (!dialog.exec()) return;
    svc_.updateStudent(dialog.result());
    if (owner_) owner_->refreshAll();
    status("学生信息已更新");
}

/* @brief 删除学生：确认 → 业务层检查未归还图书 → 刷新。 */
void StudentPanel::doDel() {
    const int id = UiUtils::selectedId(list_);
    if (id <= 0) {
        status("请先在列表中选择一名学生");
        return;
    }
    if (!UiUtils::confirm("确定删除选中的学生吗？")) return;
    svc_.removeStudent(id);
    if (owner_) owner_->refreshAll();
    status("学生已删除");
}

/* @brief 「新增」按钮回调。 */
void StudentPanel::onAdd(Fl_Widget*, void* data) { invoke<StudentPanel, &StudentPanel::doAdd>(nullptr, data); }
/* @brief 「编辑」按钮回调。 */
void StudentPanel::onEdit(Fl_Widget*, void* data) { invoke<StudentPanel, &StudentPanel::doEdit>(nullptr, data); }
/* @brief 「删除」按钮回调。 */
void StudentPanel::onDel(Fl_Widget*, void* data) { invoke<StudentPanel, &StudentPanel::doDel>(nullptr, data); }
/* @brief 「刷新」按钮回调（refresh 为无参成员，可直接套用 invoke 模板）。 */
void StudentPanel::onRefresh(Fl_Widget*, void* data) { invoke<StudentPanel, &StudentPanel::refresh>(nullptr, data); }

// ==================== ShelfPanel（书架管理页） ====================

/* @brief 构造书架管理页：4 个按钮 + 7 列列表。 */
ShelfPanel::ShelfPanel(int x, int y, int w, int h,
                       LibraryService& svc, MainWindow* owner)
    : Fl_Group(x, y, w, h, "书架管理"), svc_(svc), owner_(owner) {
    const int columns[] = {60, 110, 180, 140, 100, 110, 110, 0};  ///< 7 列宽 + 0 结束
    for (int i = 0; i < 8; ++i) widths_[i] = columns[i];
    btnAdd_ = new Fl_Button(x + 20, y + 20, 80, 28, "新增");///< 新增按钮
    btnAdd_->callback(onAdd, this);
    btnEdit_ = new Fl_Button(x + 110, y + 20, 80, 28, "编辑");  ///< 编辑按钮
    btnEdit_->callback(onEdit, this);
    btnDel_ = new Fl_Button(x + 200, y + 20, 80, 28, "删除");   ///< 删除按钮
    btnDel_->callback(onDel, this);
    btnRefresh_ = new Fl_Button(x + 290, y + 20, 80, 28, "刷新");  ///< 刷新按钮
    btnRefresh_->callback(onRefresh, this);
    list_ = new Fl_Hold_Browser(x + 20, y + 65, w - 40, h - 85);     ///< 书架列表
    UiUtils::setupBrowser(list_, widths_);
    end();
}

/*
 * @brief 重载书架列表，展示藏书数与在馆数。
 * @note 数据源为 v_shelf_stat 视图的聚合结果，无需在界面层逐架 COUNT。
 */
void ShelfPanel::refresh() {
    items_ = svc_.shelfStats();                               ///< 缓存统计列表
    list_->clear();
    list_->add("@b编号\t@b书架号\t@b位置\t@b分类\t@b容量\t@b藏书数\t@b在馆数");
    for (const ShelfStat& stat : items_) {
        std::string row = UiUtils::join({
            std::to_string(stat.shelf.id()),                  ///< 首列：主键
            UiUtils::clean(stat.shelf.code()),
            UiUtils::clean(stat.shelf.location()),
            statusOr(stat.shelf.category()),
            std::to_string(stat.shelf.capacity()),
            std::to_string(stat.bookCount),
            std::to_string(stat.availableCount)});
        list_->add(row.c_str());
    }
}

/* @brief 转发消息到主窗口状态栏。 */
void ShelfPanel::status(const std::string& message) {
    if (owner_) owner_->setStatus(message);
}

/* @brief 新增书架：模态对话框 → 写库 → 全局刷新。 */
void ShelfPanel::doAdd() {
    ShelfDialog dialog{Bookshelf{}};                          ///< 花括号构造避免 vexing parse
    if (!dialog.exec()) return;
    svc_.addShelf(dialog.result());
    if (owner_) owner_->refreshAll();
    status("新增书架成功");
}

/* @brief 编辑书架：从 ShelfStat 缓存取原实体 → 对话框 → 更新。 */
void ShelfPanel::doEdit() {
    const int id = UiUtils::selectedId(list_);
    if (id <= 0) {
        status("请先在列表中选择一个书架");
        return;
    }
    Bookshelf current;                                        ///< 原实体
    bool found = false;
    for (const ShelfStat& stat : items_) {
        if (stat.shelf.id() == id) {
            current = stat.shelf;                             ///< 从统计结构中取出书架本体
            found = true;
            break;
        }
    }
    if (!found) throw NotFoundException("书架不在当前列表中，请刷新后重试");
    ShelfDialog dialog(current);
    if (!dialog.exec()) return;
    svc_.updateShelf(dialog.result());
    if (owner_) owner_->refreshAll();
    status("书架信息已更新");
}

/* @brief 删除书架：确认 → 业务层检查藏书数 → 刷新。 */
void ShelfPanel::doDel() {
    const int id = UiUtils::selectedId(list_);
    if (id <= 0) {
        status("请先在列表中选择一个书架");
        return;
    }
    if (!UiUtils::confirm("确定删除选中的书架吗？")) return;
    svc_.removeShelf(id);
    if (owner_) owner_->refreshAll();
    status("书架已删除");
}

/* @brief 「新增」按钮回调。 */
void ShelfPanel::onAdd(Fl_Widget*, void* data) { invoke<ShelfPanel, &ShelfPanel::doAdd>(nullptr, data); }
/* @brief 「编辑」按钮回调。 */
void ShelfPanel::onEdit(Fl_Widget*, void* data) { invoke<ShelfPanel, &ShelfPanel::doEdit>(nullptr, data); }
/* @brief 「删除」按钮回调。 */
void ShelfPanel::onDel(Fl_Widget*, void* data) { invoke<ShelfPanel, &ShelfPanel::doDel>(nullptr, data); }
/* @brief 「刷新」按钮回调。 */
void ShelfPanel::onRefresh(Fl_Widget*, void* data) { invoke<ShelfPanel, &ShelfPanel::refresh>(nullptr, data); }

// ==================== LoanPanel（借阅管理页） ====================

/* @brief 构造借阅管理页：两个下拉框、筛选框、三个按钮与借阅记录列表。 */
LoanPanel::LoanPanel(int x, int y, int w, int h,
                     LibraryService& svc, MainWindow* owner)
    : Fl_Group(x, y, w, h, "借阅管理"), svc_(svc), owner_(owner) {
    const int columns[] = {70, 110, 100, 140, 230, 110, 110, 90, 0};  ///< 8 列宽 + 0 结束
    for (int i = 0; i < 9; ++i) widths_[i] = columns[i];

    studentChoice_ = new Fl_Choice(x + 70, y + 20, 240, 28, "学生");     ///< 借书学生下拉
    bookChoice_ = new Fl_Choice(x + 390, y + 20, 330, 28, "在馆图书");   ///< 可借图书下拉
    btnBorrow_ = new Fl_Button(x + 740, y + 20, 90, 28, "借出");         ///< 借出按钮
    btnBorrow_->callback(onBorrow, this);
    filterChoice_ = new Fl_Choice(x + 20, y + 62, 150, 28, "筛选");      ///< 记录筛选下拉
    filterChoice_->add("全部记录");                           ///< 选项 0 → LoanFilter::All
    filterChoice_->add("当前借阅");                           ///< 选项 1 → LoanFilter::Active
    filterChoice_->add("逾期记录");                           ///< 选项 2 → LoanFilter::Overdue
    filterChoice_->value(1);                                  ///< 默认显示当前借阅
    filterChoice_->callback(onFilter, this);
    btnReturn_ = new Fl_Button(x + 190, y + 62, 90, 28, "还书");         ///< 还书按钮
    btnReturn_->callback(onReturn, this);
    btnRefresh_ = new Fl_Button(x + 290, y + 62, 90, 28, "刷新");        ///< 刷新按钮
    btnRefresh_->callback(onRefresh, this);

    list_ = new Fl_Hold_Browser(x + 20, y + 105, w - 40, h - 125);       ///< 借阅记录列表
    UiUtils::setupBrowser(list_, widths_);
    end();
}

/*
 * @brief 重建「学生」与「在馆图书」两个下拉框。
 * @note 主要算法（平行数组 + 指针稳定性）：
 *       1) 分别收集显示标签与对应的 ID 数组（下标一一对应）；
 *       2) 先把两组标签全部写入成员 labels_，再统一调用 add(label.c_str())——
 *          若边 push 边 add，vector 扩容会让先前传入的 c_str() 悬垂；
 *       3) 用户选择下拉项后，用 value() 下标到 studentIds_/bookIds_ 取真实数据库 ID。
 */
void LoanPanel::refreshChoices() {
    std::vector<std::string> studentLabels;                   ///< 学生下拉显示文本
    std::vector<std::string> bookLabels;                      ///< 图书下拉显示文本
    studentIds_.clear();                                      ///< 清空旧 ID 映射
    bookIds_.clear();
    for (const Student& student : svc_.listStudents()) {
        studentLabels.push_back(student.name() + " (" + student.studentNo() + ")");
        studentIds_.push_back(student.id());                  ///< 与标签同下标
    }
    for (const Book& book : svc_.availableBooks()) {
        bookLabels.push_back(book.title() + " [" + book.isbn() + "]");
        bookIds_.push_back(book.id());                        ///< 与标签同下标
    }
    labels_.clear();
    labels_.insert(labels_.end(), studentLabels.begin(), studentLabels.end());  ///< 先全部落地
    const std::size_t offset = studentLabels.size();         ///< 图书标签在 labels_ 中的起始偏移
    labels_.insert(labels_.end(), bookLabels.begin(), bookLabels.end());

    studentChoice_->clear();                                  ///< 清空旧菜单项
    for (std::size_t i = 0; i < studentLabels.size(); ++i)
        studentChoice_->add(labels_[i].c_str());              ///< 指针此时稳定
    if (!studentIds_.empty()) studentChoice_->value(0);      ///< 默认选中第一项

    bookChoice_->clear();
    for (std::size_t j = 0; j < bookLabels.size(); ++j)
        bookChoice_->add(labels_[offset + j].c_str());
    if (!bookIds_.empty()) bookChoice_->value(0);
}

/*
 * @brief 按当前筛选条件重载借阅记录列表。
 * @note 算法：把筛选下拉的序号直接映射为 LoanFilter 枚举（0/1/2），
 *       调 Service 取回 LoanDetail（JOIN 已包含学生、图书、书架字段），
 *       逐行拼接为 8 列，首列仍是记录主键（还书时解析）。
 */
void LoanPanel::refresh() {
    const LoanFilter filter =
        static_cast<LoanFilter>(filterChoice_->value());      ///< 下拉序号 → 枚举
    items_ = svc_.listLoans(filter);                          ///< 缓存明细列表
    list_->clear();
    list_->add("@b记录号\t@b学号\t@b姓名\t@bISBN\t@b题名\t@b借出日\t@b应还日\t@b状态");
    for (const LoanDetail& detail : items_) {
        std::string row = UiUtils::join({
            std::to_string(detail.recordId()),                ///< 首列：记录主键
            UiUtils::clean(detail.studentNo),
            UiUtils::clean(detail.studentName),
            UiUtils::clean(detail.isbn),
            UiUtils::clean(detail.bookTitle),
            detail.record.borrowDate(),
            detail.record.dueDate(),
            detail.statusText()});
        list_->add(row.c_str());
    }
}

/* @brief 转发消息到主窗口状态栏。 */
void LoanPanel::status(const std::string& message) {
    if (owner_) owner_->setStatus(message);
}

/*
 * @brief 借书流程：把两个下拉框的下标还原为 ID，调用业务层并全局刷新。
 * @note 借书成功后必须 refreshAll()：在馆图书下拉、记录列表、统计条都要同步，
 *       否则界面会残留已借出的图书，导致用户重复点击并触发业务异常。
 */
void LoanPanel::doBorrow() {
    const int studentIndex = studentChoice_->value();         ///< 学生下拉下标
    const int bookIndex = bookChoice_->value();               ///< 图书下拉下标
    if (studentIndex < 0 || bookIndex < 0 ||
        studentIndex >= static_cast<int>(studentIds_.size()) ||
        bookIndex >= static_cast<int>(bookIds_.size())) {
        status("请先选择学生和在馆图书");                      ///< 下标越界/未选择
        return;
    }
    svc_.borrowBook(studentIds_[studentIndex],               ///< 下标 → 学生 ID
                    bookIds_[bookIndex]);                    ///< 下标 → 图书 ID
    if (owner_) owner_->refreshAll();                         ///< 同步所有页签与统计
    status("借书成功，借期 30 天");
}

/* @brief 还书流程：解析选中记录 ID → 二次确认 → 业务层事务 → 全局刷新。 */
void LoanPanel::doReturn() {
    const int recordId = UiUtils::selectedId(list_);          ///< 选中行首列即记录 ID
    if (recordId <= 0) {
        status("请先在列表中选择一条未归还的借阅记录");
        return;
    }
    if (!UiUtils::confirm("确认归还选中的图书吗？")) return;
    svc_.returnBook(recordId);                               ///< 事务内更新记录与图书状态
    if (owner_) owner_->refreshAll();
    status("还书成功");
}

/* @brief 「借出」按钮回调。 */
void LoanPanel::onBorrow(Fl_Widget*, void* data) { invoke<LoanPanel, &LoanPanel::doBorrow>(nullptr, data); }
/* @brief 「还书」按钮回调。 */
void LoanPanel::onReturn(Fl_Widget*, void* data) { invoke<LoanPanel, &LoanPanel::doReturn>(nullptr, data); }

/*
 * @brief 「刷新」按钮回调：同时重建下拉框与记录列表。
 * @note 单独实现是因为需要依次调用两个成员动作，不便套用单方法 invoke 模板。
 */
void LoanPanel::onRefresh(Fl_Widget*, void* data) {
    LoanPanel* self = static_cast<LoanPanel*>(data);
    try {
        self->refreshChoices();                               ///< 先更新下拉数据源
        self->refresh();                                     ///< 再更新记录列表
        self->status("借阅数据已刷新");
    } catch (const std::exception& error) {
        UiUtils::error(error.what());
    }
}

/* @brief 「筛选」下拉回调：按新条件重绘列表。 */
void LoanPanel::onFilter(Fl_Widget*, void* data) { invoke<LoanPanel, &LoanPanel::refresh>(nullptr, data); }

// ==================== MainWindow（主窗口） ====================

/*
 * @brief 构造主窗口并组装整棵控件树，最后触发首次数据刷新。
 * @param w,h 窗口尺寸（1024x680）。
 * @param title 窗口标题。
 * @param svc 业务门面引用。
 * @note 算法：Fl_Double_Window 构造自动 begin() → 添加统计条/按钮 →
 *       new Fl_Tabs 自动 begin() → 依次 new 四个 Panel（各自构造末尾 end() 会把当前组还原为 Tabs）
 *       → tabs_->end() → 添加状态栏 → end() 结束窗口 → resizable(tabs_) 允许拉伸 →
 *       refreshAll() 完成首屏数据装载。
 */
MainWindow::MainWindow(int w, int h, const char* title, LibraryService& svc)
    : Fl_Double_Window(w, h, title), svc_(svc) {
    topBar_ = new Fl_Box(10, 10, 760, 30, "");               ///< 顶部统计信息条
    topBar_->box(FL_THIN_UP_BOX);                            ///< 轻微凸起边框
    topBar_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);         ///< 文本左对齐
    btnRefresh_ = new Fl_Button(790, 10, 100, 30, "刷新全部");  ///< 全局刷新按钮
    btnRefresh_->callback(onRefresh, this);
    btnAbout_ = new Fl_Button(905, 10, 100, 30, "关于");     ///< 关于按钮
    btnAbout_->callback(onAbout, this);

    tabs_ = new Fl_Tabs(10, 52, 1004, 570);                  ///< 四个页签容器
    bookPanel_ = new BookPanel(10, 82, 1004, 535, svc_, this);     ///< 页签 1：图书管理
    studentPanel_ = new StudentPanel(10, 82, 1004, 535, svc_, this);///< 页签 2：学生管理
    shelfPanel_ = new ShelfPanel(10, 82, 1004, 535, svc_, this);    ///< 页签 3：书架管理
    loanPanel_ = new LoanPanel(10, 82, 1004, 535, svc_, this);      ///< 页签 4：借阅管理
    tabs_->end();                                             ///< 结束 Tabs 控件组

    statusBar_ = new Fl_Box(10, 630, 1004, 36, "就绪");      ///< 底部状态栏
    statusBar_->box(FL_DOWN_BOX);
    statusBar_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    end();                                                   ///< 结束主窗口控件组
    resizable(tabs_);                                        ///< 窗口缩放时由 Tabs 区域伸缩
    refreshAll();                                            ///< 首屏数据装载
}

/*
 * @brief 更新底部状态栏。
 * @param message 操作结果或错误原因。
 * @note copy_label 会复制字符串，故可安全传入临时拼接的 c_str()。
 */
void MainWindow::setStatus(const std::string& message) {
    statusBar_->copy_label(("提示：" + message).c_str());
    statusBar_->redraw();                                     ///< 立即重绘
}

/*
 * @brief 全局刷新：统计条 + 四个页签 + 借阅下拉框。
 * @note 主要算法（保证界面与数据库一致）：
 *       1) 先 refreshOverdue() 批量标记逾期（写操作，幂等）；
 *       2) 取 statistics() 更新统计条；
 *       3) 依次刷新图书、学生、书架三个列表；
 *       4) 重建借阅页下拉框（在馆图书可能已变化）并刷新记录列表；
 *       5) 全流程 try/catch，任一步失败只在状态栏提示，不让窗口崩溃或半刷新。
 */
void MainWindow::refreshAll() {
    try {
        svc_.refreshOverdue();                               ///< 1) 逾期状态维护
        topBar_->copy_label(svc_.statistics().summary().c_str());  ///< 2) 统计条
        bookPanel_->refresh();                               ///< 3) 三个基础列表
        studentPanel_->refresh();
        shelfPanel_->refresh();
        loanPanel_->refreshChoices();                        ///< 4) 借阅页下拉 + 列表
        loanPanel_->refresh();
        statusBar_->copy_label("提示：数据已刷新");
    } catch (const std::exception& error) {
        setStatus(std::string("刷新失败：") + error.what());  ///< 5) 异常降级提示
    }
}

/* @brief 「刷新全部」按钮回调。 */
void MainWindow::onRefresh(Fl_Widget*, void* data) {
    static_cast<MainWindow*>(data)->refreshAll();
}

/* @brief 「关于」按钮回调：显示版本与技术栈信息。 */
void MainWindow::onAbout(Fl_Widget*, void*) {
    fl_message("简易图书借阅系统 V1.0\n\n"
               "C++17 面向对象 · FLTK 1.4.5 图形界面 · SQLite 3 数据库\n"
               "四层架构：界面 / 业务逻辑 / 领域模型 / 数据访问\n"
               "课程设计示例项目");
}

}  // namespace lls
