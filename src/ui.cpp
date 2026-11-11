// ui.cpp -- FLTK widgets, modal dialogs, panels and event callbacks
#include "ui.h"

#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace lls {
namespace {

Fl_Input* makeInput(int y, const char* label, int width = 290) {
    Fl_Box* caption = new Fl_Box(20, y, 110, 26, label);
    caption->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    return new Fl_Input(140, y, width, 26);
}

Fl_Int_Input* makeIntInput(int y, const char* label) {
    Fl_Box* caption = new Fl_Box(20, y, 110, 26, label);
    caption->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    return new Fl_Int_Input(140, y, 290, 26);
}

Fl_Choice* makeChoice(int y, const char* label) {
    Fl_Box* caption = new Fl_Box(20, y, 110, 26, label);
    caption->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    return new Fl_Choice(140, y, 290, 26);
}

// Uniform exception boundary for every FLTK callback.
template <typename Panel, void (Panel::*Method)()>
void invoke(Fl_Widget*, void* data) {
    Panel* self = static_cast<Panel*>(data);
    try {
        (self->*Method)();
    } catch (const AppException& error) {
        UiUtils::error(error.what());
        self->status(error.what());
    } catch (const std::exception& error) {
        UiUtils::error(error.what());
        self->status(error.what());
    }
}

std::string statusOr(const std::string& value) {
    return UiUtils::clean(value.empty() ? std::string("-") : value);
}

}  // namespace

// ---------------------------------------------------------------- UiUtils
void UiUtils::info(const std::string& message) {
    fl_alert("%s", message.c_str());
}

void UiUtils::error(const std::string& message) {
    fl_alert("操作失败：\n%s", message.c_str());
}

bool UiUtils::confirm(const std::string& message) {
    return fl_choice("%s", "取消", "确定", static_cast<const char*>(nullptr),
                     message.c_str()) == 1;
}

std::string UiUtils::text(Fl_Input* input) {
    const char* value = input ? input->value() : nullptr;
    return value ? std::string(value) : std::string();
}

int UiUtils::intValue(Fl_Input* input) {
    std::string value = trim(text(input));
    if (value.empty()) return 0;
    try {
        return std::stoi(value);
    } catch (...) {
        return 0;
    }
}

std::string UiUtils::clean(const std::string& value) {
    std::string result;
    for (char ch : value) {
        if (ch == '\t' || ch == '\n' || ch == '\r')
            result += ' ';
        else if (ch == '@')
            result += "@@";  // escape FLTK format character
        else
            result += ch;
    }
    return result;
}

std::string UiUtils::join(const std::vector<std::string>& columns) {
    std::string result;
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (i) result += '\t';
        result += columns[i];
    }
    return result;
}

void UiUtils::setupBrowser(Fl_Hold_Browser* browser, const int* widths) {
    browser->column_widths(widths);
    browser->column_char('\t');
    browser->textsize(13);
    browser->box(FL_DOWN_BOX);
}

int UiUtils::selectedId(Fl_Hold_Browser* browser) {
    int line = browser->value();
    if (line <= 1) return 0;  // line 1 is the header row
    const char* raw = browser->text(line);
    if (!raw) return 0;
    std::string row(raw);
    std::size_t tab = row.find('\t');
    std::string first = tab == std::string::npos ? row : row.substr(0, tab);
    try {
        return std::stoi(first);
    } catch (...) {
        return 0;
    }
}

// ---------------------------------------------------------------- DialogBase
DialogBase::DialogBase(int w, int h, const char* title) : Fl_Window(w, h, title) {
    set_modal();
}

bool DialogBase::exec() {
    show();
    while (shown()) Fl::wait();
    return confirmed_;
}

void DialogBase::onCancel(Fl_Widget*, void* data) {
    DialogBase* self = static_cast<DialogBase*>(data);
    self->confirmed_ = false;
    self->hide();
}

// ---------------------------------------------------------------- BookDialog
BookDialog::BookDialog(const Book& initial, const std::vector<Bookshelf>& shelves)
    : DialogBase(460, 430, initial.id() > 0 ? "编辑图书" : "新增图书"), result_(initial) {
    isbn_ = makeInput(20, "ISBN");
    title_ = makeInput(56, "题名");
    author_ = makeInput(92, "作者");
    publisher_ = makeInput(128, "出版社");
    year_ = makeIntInput(164, "出版年");
    category_ = makeInput(200, "分类");
    shelf_ = makeChoice(236, "所属书架");
    status_ = makeChoice(272, "状态");

    isbn_->value(result_.isbn().c_str());
    title_->value(result_.title().c_str());
    author_->value(result_.author().c_str());
    publisher_->value(result_.publisher().c_str());
    category_->value(result_.category().c_str());
    year_->value(result_.publishYear() > 0
                     ? std::to_string(result_.publishYear()).c_str()
                     : "");

    for (const Bookshelf& shelf : shelves) {
        labels_.push_back(shelf.code() + "  " + shelf.location());
        shelfIds_.push_back(shelf.id());
    }
    for (const std::string& label : labels_) shelf_->add(label.c_str());
    for (int i = 0; i < static_cast<int>(shelfIds_.size()); ++i) {
        if (shelfIds_[i] == result_.shelfId()) shelf_->value(i);
    }
    if (shelfIds_.empty()) shelf_->add("（请先创建书架）");

    status_->add("在馆");
    status_->add("借出");
    status_->add("遗失");
    status_->add("维修");
    status_->value(static_cast<int>(result_.status()));

    Fl_Button* ok = new Fl_Button(240, 370, 90, 32, "确定");
    ok->callback(onOk, this);
    Fl_Button* cancel = new Fl_Button(340, 370, 90, 32, "取消");
    cancel->callback(DialogBase::onCancel, this);
    end();
}

void BookDialog::onOk(Fl_Widget*, void* data) {
    static_cast<BookDialog*>(data)->collect();
}

void BookDialog::collect() {
    result_.setIsbn(UiUtils::text(isbn_));
    result_.setTitle(UiUtils::text(title_));
    result_.setAuthor(UiUtils::text(author_));
    result_.setPublisher(UiUtils::text(publisher_));
    result_.setCategory(UiUtils::text(category_));
    result_.setPublishYear(UiUtils::intValue(year_));
    int index = shelf_->value();
    if (index >= 0 && index < static_cast<int>(shelfIds_.size()))
        result_.setShelfId(shelfIds_[index]);
    result_.setStatus(Book::statusFromInt(status_->value()));
    const std::string error = result_.validate();
    if (!error.empty()) {
        UiUtils::error(error);
        return;
    }
    confirmed_ = true;
    hide();
}

// ---------------------------------------------------------------- StudentDialog
StudentDialog::StudentDialog(const Student& initial)
    : DialogBase(460, 340, initial.id() > 0 ? "编辑学生" : "新增学生"), result_(initial) {
    studentNo_ = makeInput(20, "学号");
    name_ = makeInput(56, "姓名");
    major_ = makeInput(92, "专业班级");
    phone_ = makeInput(128, "联系电话");
    maxBorrow_ = makeIntInput(164, "可借上限");
    active_ = new Fl_Check_Button(140, 200, 220, 26, "启用（允许借书）");

    studentNo_->value(result_.studentNo().c_str());
    name_->value(result_.name().c_str());
    major_->value(result_.major().c_str());
    phone_->value(result_.phone().c_str());
    if (result_.id() == 0) result_.setMaxBorrow(Student::defaultMaxBorrow());
    maxBorrow_->value(std::to_string(result_.maxBorrow()).c_str());
    active_->value(result_.active() ? 1 : 0);

    Fl_Button* ok = new Fl_Button(240, 280, 90, 32, "确定");
    ok->callback(onOk, this);
    Fl_Button* cancel = new Fl_Button(340, 280, 90, 32, "取消");
    cancel->callback(DialogBase::onCancel, this);
    end();
}

void StudentDialog::onOk(Fl_Widget*, void* data) {
    static_cast<StudentDialog*>(data)->collect();
}

void StudentDialog::collect() {
    result_.setStudentNo(UiUtils::text(studentNo_));
    result_.setName(UiUtils::text(name_));
    result_.setMajor(UiUtils::text(major_));
    result_.setPhone(UiUtils::text(phone_));
    result_.setMaxBorrow(UiUtils::intValue(maxBorrow_));
    result_.setActive(active_->value() != 0);
    const std::string error = result_.validate();
    if (!error.empty()) {
        UiUtils::error(error);
        return;
    }
    confirmed_ = true;
    hide();
}

// ---------------------------------------------------------------- ShelfDialog
ShelfDialog::ShelfDialog(const Bookshelf& initial)
    : DialogBase(460, 340, initial.id() > 0 ? "编辑书架" : "新增书架"), result_(initial) {
    code_ = makeInput(20, "书架编号");
    location_ = makeInput(56, "所在位置");
    category_ = makeInput(92, "分类");
    capacity_ = makeIntInput(128, "容量（册）");
    remark_ = makeInput(164, "备注");

    code_->value(result_.code().c_str());
    location_->value(result_.location().c_str());
    category_->value(result_.category().c_str());
    remark_->value(result_.remark().c_str());
    capacity_->value(result_.capacity() > 0
                         ? std::to_string(result_.capacity()).c_str()
                         : "30");

    Fl_Button* ok = new Fl_Button(240, 280, 90, 32, "确定");
    ok->callback(onOk, this);
    Fl_Button* cancel = new Fl_Button(340, 280, 90, 32, "取消");
    cancel->callback(DialogBase::onCancel, this);
    end();
}

void ShelfDialog::onOk(Fl_Widget*, void* data) {
    static_cast<ShelfDialog*>(data)->collect();
}

void ShelfDialog::collect() {
    result_.setCode(UiUtils::text(code_));
    result_.setLocation(UiUtils::text(location_));
    result_.setCategory(UiUtils::text(category_));
    result_.setRemark(UiUtils::text(remark_));
    result_.setCapacity(UiUtils::intValue(capacity_));
    const std::string error = result_.validate();
    if (!error.empty()) {
        UiUtils::error(error);
        return;
    }
    confirmed_ = true;
    hide();
}

// ---------------------------------------------------------------- BookPanel
BookPanel::BookPanel(int x, int y, int w, int h, LibraryService& svc, MainWindow* owner)
    : Fl_Group(x, y, w, h, "图书管理"), svc_(svc), owner_(owner) {
    const int columns[] = {60, 150, 260, 150, 110, 90, 90, 0};
    for (int i = 0; i < 8; ++i) widths_[i] = columns[i];

    search_ = new Fl_Input(x + 20, y + 20, 260, 28);
    search_->callback(onSearch, this);
    btnSearch_ = new Fl_Button(x + 290, y + 20, 80, 28, "查询");
    btnSearch_->callback(onSearch, this);
    btnAdd_ = new Fl_Button(x + 390, y + 20, 80, 28, "新增");
    btnAdd_->callback(onAdd, this);
    btnEdit_ = new Fl_Button(x + 480, y + 20, 80, 28, "编辑");
    btnEdit_->callback(onEdit, this);
    btnDel_ = new Fl_Button(x + 570, y + 20, 80, 28, "删除");
    btnDel_->callback(onDel, this);
    btnRefresh_ = new Fl_Button(x + 660, y + 20, 80, 28, "刷新");
    btnRefresh_->callback(onRefresh, this);

    list_ = new Fl_Hold_Browser(x + 20, y + 65, w - 40, h - 85);
    UiUtils::setupBrowser(list_, widths_);
    end();
}

void BookPanel::refresh(const std::string& keyword) {
    items_ = svc_.listBooks(keyword);
    std::map<int, std::string> shelfCodes;
    for (const Bookshelf& shelf : svc_.listShelves()) shelfCodes[shelf.id()] = shelf.code();
    list_->clear();
    list_->add("@b编号\t@bISBN\t@b题名\t@b作者\t@b书架\t@b状态\t@b出版年");
    for (const Book& book : items_) {
        std::string code = shelfCodes.count(book.shelfId())
                               ? shelfCodes[book.shelfId()]
                               : std::to_string(book.shelfId());
        std::string row = UiUtils::join({
            std::to_string(book.id()), UiUtils::clean(book.isbn()),
            UiUtils::clean(book.title()), statusOr(book.author()),
            UiUtils::clean(code), book.statusText(),
            book.publishYear() > 0 ? std::to_string(book.publishYear()) : "-"});
        list_->add(row.c_str());
    }
}

void BookPanel::status(const std::string& message) {
    if (owner_) owner_->setStatus(message);
}

void BookPanel::doAdd() {
    BookDialog dialog{Book{}, svc_.listShelves()};
    if (!dialog.exec()) return;
    svc_.addBook(dialog.result());
    if (owner_) owner_->refreshAll();
    status("新增图书成功");
}

void BookPanel::doEdit() {
    const int id = UiUtils::selectedId(list_);
    if (id <= 0) {
        status("请先在列表中选择一本图书");
        return;
    }
    Book current;
    bool found = false;
    for (const Book& book : items_) {
        if (book.id() == id) {
            current = book;
            found = true;
            break;
        }
    }
    if (!found) throw NotFoundException("图书不在当前列表中，请刷新后重试");
    BookDialog dialog(current, svc_.listShelves());
    if (!dialog.exec()) return;
    svc_.updateBook(dialog.result());
    if (owner_) owner_->refreshAll();
    status("图书信息已更新");
}

void BookPanel::doDel() {
    const int id = UiUtils::selectedId(list_);
    if (id <= 0) {
        status("请先在列表中选择一本图书");
        return;
    }
    if (!UiUtils::confirm("确定删除选中的图书吗？")) return;
    svc_.removeBook(id);
    if (owner_) owner_->refreshAll();
    status("图书已删除");
}

void BookPanel::onSearch(Fl_Widget*, void* data) {
    BookPanel* self = static_cast<BookPanel*>(data);
    try {
        self->refresh(UiUtils::text(self->search_));
        self->status("检索完成");
    } catch (const std::exception& error) {
        UiUtils::error(error.what());
    }
}
void BookPanel::onAdd(Fl_Widget*, void* data) { invoke<BookPanel, &BookPanel::doAdd>(nullptr, data); }
void BookPanel::onEdit(Fl_Widget*, void* data) { invoke<BookPanel, &BookPanel::doEdit>(nullptr, data); }
void BookPanel::onDel(Fl_Widget*, void* data) { invoke<BookPanel, &BookPanel::doDel>(nullptr, data); }
void BookPanel::onRefresh(Fl_Widget*, void* data) {
    BookPanel* self = static_cast<BookPanel*>(data);
    try {
        self->search_->value("");
        self->refresh();
        self->status("图书列表已刷新");
    } catch (const std::exception& error) {
        UiUtils::error(error.what());
    }
}

// ---------------------------------------------------------------- StudentPanel
StudentPanel::StudentPanel(int x, int y, int w, int h, LibraryService& svc, MainWindow* owner)
    : Fl_Group(x, y, w, h, "学生管理"), svc_(svc), owner_(owner) {
    const int columns[] = {60, 120, 110, 200, 130, 100, 90, 100, 0};
    for (int i = 0; i < 9; ++i) widths_[i] = columns[i];
    btnAdd_ = new Fl_Button(x + 20, y + 20, 80, 28, "新增");
    btnAdd_->callback(onAdd, this);
    btnEdit_ = new Fl_Button(x + 110, y + 20, 80, 28, "编辑");
    btnEdit_->callback(onEdit, this);
    btnDel_ = new Fl_Button(x + 200, y + 20, 80, 28, "删除");
    btnDel_->callback(onDel, this);
    btnRefresh_ = new Fl_Button(x + 290, y + 20, 80, 28, "刷新");
    btnRefresh_->callback(onRefresh, this);
    list_ = new Fl_Hold_Browser(x + 20, y + 65, w - 40, h - 85);
    UiUtils::setupBrowser(list_, widths_);
    end();
}

void StudentPanel::refresh() {
    items_ = svc_.listStudents();
    list_->clear();
    list_->add("@b编号\t@b学号\t@b姓名\t@b专业班级\t@b电话\t@b上限\t@b状态\t@b已借");
    for (const Student& student : items_) {
        const int borrowed = svc_.activeLoanCount(student.id());
        std::string row = UiUtils::join({
            std::to_string(student.id()), UiUtils::clean(student.studentNo()),
            UiUtils::clean(student.name()), statusOr(student.major()),
            statusOr(student.phone()), std::to_string(student.maxBorrow()),
            student.active() ? "启用" : "停用", std::to_string(borrowed)});
        list_->add(row.c_str());
    }
}

void StudentPanel::status(const std::string& message) {
    if (owner_) owner_->setStatus(message);
}

void StudentPanel::doAdd() {
    StudentDialog dialog{Student{}};
    if (!dialog.exec()) return;
    svc_.addStudent(dialog.result());
    if (owner_) owner_->refreshAll();
    status("新增学生成功");
}

void StudentPanel::doEdit() {
    const int id = UiUtils::selectedId(list_);
    if (id <= 0) {
        status("请先在列表中选择一名学生");
        return;
    }
    Student current;
    bool found = false;
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

void StudentPanel::onAdd(Fl_Widget*, void* data) { invoke<StudentPanel, &StudentPanel::doAdd>(nullptr, data); }
void StudentPanel::onEdit(Fl_Widget*, void* data) { invoke<StudentPanel, &StudentPanel::doEdit>(nullptr, data); }
void StudentPanel::onDel(Fl_Widget*, void* data) { invoke<StudentPanel, &StudentPanel::doDel>(nullptr, data); }
void StudentPanel::onRefresh(Fl_Widget*, void* data) { invoke<StudentPanel, &StudentPanel::refresh>(nullptr, data); }

// ---------------------------------------------------------------- ShelfPanel
ShelfPanel::ShelfPanel(int x, int y, int w, int h, LibraryService& svc, MainWindow* owner)
    : Fl_Group(x, y, w, h, "书架管理"), svc_(svc), owner_(owner) {
    const int columns[] = {60, 110, 180, 140, 100, 110, 110, 0};
    for (int i = 0; i < 8; ++i) widths_[i] = columns[i];
    btnAdd_ = new Fl_Button(x + 20, y + 20, 80, 28, "新增");
    btnAdd_->callback(onAdd, this);
    btnEdit_ = new Fl_Button(x + 110, y + 20, 80, 28, "编辑");
    btnEdit_->callback(onEdit, this);
    btnDel_ = new Fl_Button(x + 200, y + 20, 80, 28, "删除");
    btnDel_->callback(onDel, this);
    btnRefresh_ = new Fl_Button(x + 290, y + 20, 80, 28, "刷新");
    btnRefresh_->callback(onRefresh, this);
    list_ = new Fl_Hold_Browser(x + 20, y + 65, w - 40, h - 85);
    UiUtils::setupBrowser(list_, widths_);
    end();
}

void ShelfPanel::refresh() {
    items_ = svc_.shelfStats();
    list_->clear();
    list_->add("@b编号\t@b书架号\t@b位置\t@b分类\t@b容量\t@b藏书数\t@b在馆数");
    for (const ShelfStat& stat : items_) {
        std::string row = UiUtils::join({
            std::to_string(stat.shelf.id()), UiUtils::clean(stat.shelf.code()),
            UiUtils::clean(stat.shelf.location()), statusOr(stat.shelf.category()),
            std::to_string(stat.shelf.capacity()), std::to_string(stat.bookCount),
            std::to_string(stat.availableCount)});
        list_->add(row.c_str());
    }
}

void ShelfPanel::status(const std::string& message) {
    if (owner_) owner_->setStatus(message);
}

void ShelfPanel::doAdd() {
    ShelfDialog dialog{Bookshelf{}};
    if (!dialog.exec()) return;
    svc_.addShelf(dialog.result());
    if (owner_) owner_->refreshAll();
    status("新增书架成功");
}

void ShelfPanel::doEdit() {
    const int id = UiUtils::selectedId(list_);
    if (id <= 0) {
        status("请先在列表中选择一个书架");
        return;
    }
    Bookshelf current;
    bool found = false;
    for (const ShelfStat& stat : items_) {
        if (stat.shelf.id() == id) {
            current = stat.shelf;
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

void ShelfPanel::onAdd(Fl_Widget*, void* data) { invoke<ShelfPanel, &ShelfPanel::doAdd>(nullptr, data); }
void ShelfPanel::onEdit(Fl_Widget*, void* data) { invoke<ShelfPanel, &ShelfPanel::doEdit>(nullptr, data); }
void ShelfPanel::onDel(Fl_Widget*, void* data) { invoke<ShelfPanel, &ShelfPanel::doDel>(nullptr, data); }
void ShelfPanel::onRefresh(Fl_Widget*, void* data) { invoke<ShelfPanel, &ShelfPanel::refresh>(nullptr, data); }

// ---------------------------------------------------------------- LoanPanel
LoanPanel::LoanPanel(int x, int y, int w, int h, LibraryService& svc, MainWindow* owner)
    : Fl_Group(x, y, w, h, "借阅管理"), svc_(svc), owner_(owner) {
    const int columns[] = {70, 110, 100, 140, 230, 110, 110, 90, 0};
    for (int i = 0; i < 9; ++i) widths_[i] = columns[i];

    studentChoice_ = new Fl_Choice(x + 70, y + 20, 240, 28, "学生");
    bookChoice_ = new Fl_Choice(x + 390, y + 20, 330, 28, "在馆图书");
    btnBorrow_ = new Fl_Button(x + 740, y + 20, 90, 28, "借出");
    btnBorrow_->callback(onBorrow, this);
    filterChoice_ = new Fl_Choice(x + 20, y + 62, 150, 28, "筛选");
    filterChoice_->add("全部记录");
    filterChoice_->add("当前借阅");
    filterChoice_->add("逾期记录");
    filterChoice_->value(1);
    filterChoice_->callback(onFilter, this);
    btnReturn_ = new Fl_Button(x + 190, y + 62, 90, 28, "还书");
    btnReturn_->callback(onReturn, this);
    btnRefresh_ = new Fl_Button(x + 290, y + 62, 90, 28, "刷新");
    btnRefresh_->callback(onRefresh, this);

    list_ = new Fl_Hold_Browser(x + 20, y + 105, w - 40, h - 125);
    UiUtils::setupBrowser(list_, widths_);
    end();
}

void LoanPanel::refreshChoices() {
    std::vector<std::string> studentLabels;
    std::vector<std::string> bookLabels;
    studentIds_.clear();
    bookIds_.clear();
    for (const Student& student : svc_.listStudents()) {
        studentLabels.push_back(student.name() + " (" + student.studentNo() + ")");
        studentIds_.push_back(student.id());
    }
    for (const Book& book : svc_.availableBooks()) {
        bookLabels.push_back(book.title() + " [" + book.isbn() + "]");
        bookIds_.push_back(book.id());
    }
    labels_.clear();
    labels_.insert(labels_.end(), studentLabels.begin(), studentLabels.end());
    const std::size_t offset = studentLabels.size();
    labels_.insert(labels_.end(), bookLabels.begin(), bookLabels.end());

    studentChoice_->clear();
    for (std::size_t i = 0; i < studentLabels.size(); ++i)
        studentChoice_->add(labels_[i].c_str());
    if (!studentIds_.empty()) studentChoice_->value(0);

    bookChoice_->clear();
    for (std::size_t j = 0; j < bookLabels.size(); ++j)
        bookChoice_->add(labels_[offset + j].c_str());
    if (!bookIds_.empty()) bookChoice_->value(0);
}

void LoanPanel::refresh() {
    const LoanFilter filter = static_cast<LoanFilter>(filterChoice_->value());
    items_ = svc_.listLoans(filter);
    list_->clear();
    list_->add("@b记录号\t@b学号\t@b姓名\t@bISBN\t@b题名\t@b借出日\t@b应还日\t@b状态");
    for (const LoanDetail& detail : items_) {
        std::string row = UiUtils::join({
            std::to_string(detail.recordId()),
            UiUtils::clean(detail.studentNo), UiUtils::clean(detail.studentName),
            UiUtils::clean(detail.isbn), UiUtils::clean(detail.bookTitle),
            detail.record.borrowDate(), detail.record.dueDate(),
            detail.statusText()});
        list_->add(row.c_str());
    }
}

void LoanPanel::status(const std::string& message) {
    if (owner_) owner_->setStatus(message);
}

void LoanPanel::doBorrow() {
    const int studentIndex = studentChoice_->value();
    const int bookIndex = bookChoice_->value();
    if (studentIndex < 0 || bookIndex < 0 ||
        studentIndex >= static_cast<int>(studentIds_.size()) ||
        bookIndex >= static_cast<int>(bookIds_.size())) {
        status("请先选择学生和在馆图书");
        return;
    }
    svc_.borrowBook(studentIds_[studentIndex], bookIds_[bookIndex]);
    if (owner_) owner_->refreshAll();
    status("借书成功，借期 30 天");
}

void LoanPanel::doReturn() {
    const int recordId = UiUtils::selectedId(list_);
    if (recordId <= 0) {
        status("请先在列表中选择一条未归还的借阅记录");
        return;
    }
    if (!UiUtils::confirm("确认归还选中的图书吗？")) return;
    svc_.returnBook(recordId);
    if (owner_) owner_->refreshAll();
    status("还书成功");
}

void LoanPanel::onBorrow(Fl_Widget*, void* data) { invoke<LoanPanel, &LoanPanel::doBorrow>(nullptr, data); }
void LoanPanel::onReturn(Fl_Widget*, void* data) { invoke<LoanPanel, &LoanPanel::doReturn>(nullptr, data); }
void LoanPanel::onRefresh(Fl_Widget*, void* data) {
    LoanPanel* self = static_cast<LoanPanel*>(data);
    try {
        self->refreshChoices();
        self->refresh();
        self->status("借阅数据已刷新");
    } catch (const std::exception& error) {
        UiUtils::error(error.what());
    }
}
void LoanPanel::onFilter(Fl_Widget*, void* data) { invoke<LoanPanel, &LoanPanel::refresh>(nullptr, data); }

// ---------------------------------------------------------------- MainWindow
MainWindow::MainWindow(int w, int h, const char* title, LibraryService& svc)
    : Fl_Double_Window(w, h, title), svc_(svc) {
    topBar_ = new Fl_Box(10, 10, 760, 30, "");
    topBar_->box(FL_THIN_UP_BOX);
    topBar_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    btnRefresh_ = new Fl_Button(790, 10, 100, 30, "刷新全部");
    btnRefresh_->callback(onRefresh, this);
    btnAbout_ = new Fl_Button(905, 10, 100, 30, "关于");
    btnAbout_->callback(onAbout, this);

    tabs_ = new Fl_Tabs(10, 52, 1004, 570);
    bookPanel_ = new BookPanel(10, 82, 1004, 535, svc_, this);
    studentPanel_ = new StudentPanel(10, 82, 1004, 535, svc_, this);
    shelfPanel_ = new ShelfPanel(10, 82, 1004, 535, svc_, this);
    loanPanel_ = new LoanPanel(10, 82, 1004, 535, svc_, this);
    tabs_->end();

    statusBar_ = new Fl_Box(10, 630, 1004, 36, "就绪");
    statusBar_->box(FL_DOWN_BOX);
    statusBar_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    end();
    resizable(tabs_);
    refreshAll();
}

void MainWindow::setStatus(const std::string& message) {
    statusBar_->copy_label(("提示：" + message).c_str());
    statusBar_->redraw();
}

void MainWindow::refreshAll() {
    try {
        svc_.refreshOverdue();
        topBar_->copy_label(svc_.statistics().summary().c_str());
        bookPanel_->refresh();
        studentPanel_->refresh();
        shelfPanel_->refresh();
        loanPanel_->refreshChoices();
        loanPanel_->refresh();
        statusBar_->copy_label("提示：数据已刷新");
    } catch (const std::exception& error) {
        setStatus(std::string("刷新失败：") + error.what());
    }
}

void MainWindow::onRefresh(Fl_Widget*, void* data) {
    static_cast<MainWindow*>(data)->refreshAll();
}

void MainWindow::onAbout(Fl_Widget*, void*) {
    fl_message("简易图书借阅系统 V1.0\n\n"
               "C++17 面向对象 · FLTK 1.4.5 图形界面 · SQLite 3 数据库\n"
               "四层架构：界面 / 业务逻辑 / 领域模型 / 数据访问\n"
               "课程设计示例项目");
}

}  // namespace lls
