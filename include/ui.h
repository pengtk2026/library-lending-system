#pragma once
// ui.h -- FLTK presentation layer declarations
#include "service.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Widget.H>
#include <FL/Fl_Window.H>
#include <FL/fl_ask.H>

#include <string>
#include <vector>

namespace lls {

class MainWindow;

class UiUtils {
public:
    static void info(const std::string& message);
    static void error(const std::string& message);
    static bool confirm(const std::string& message);
    static std::string text(Fl_Input* input);
    static int intValue(Fl_Input* input);
    static void setupBrowser(Fl_Hold_Browser* browser, const int* widths);
    static int selectedId(Fl_Hold_Browser* browser);
    static std::string clean(const std::string& value);
    static std::string join(const std::vector<std::string>& columns);
};

class DialogBase : public Fl_Window {
protected:
    bool confirmed_ = false;

public:
    DialogBase(int w, int h, const char* title);
    bool exec();
    static void onCancel(Fl_Widget*, void* data);
};

class BookDialog : public DialogBase {
    Fl_Input* isbn_;
    Fl_Input* title_;
    Fl_Input* author_;
    Fl_Input* publisher_;
    Fl_Input* category_;
    Fl_Int_Input* year_;
    Fl_Choice* shelf_;
    Fl_Choice* status_;
    std::vector<int> shelfIds_;
    std::vector<std::string> labels_;
    Book result_;

public:
    BookDialog(const Book& initial, const std::vector<Bookshelf>& shelves);
    const Book& result() const noexcept { return result_; }
    static void onOk(Fl_Widget*, void* data);
    void collect();
};

class StudentDialog : public DialogBase {
    Fl_Input* studentNo_;
    Fl_Input* name_;
    Fl_Input* major_;
    Fl_Input* phone_;
    Fl_Int_Input* maxBorrow_;
    Fl_Check_Button* active_;
    Student result_;

public:
    explicit StudentDialog(const Student& initial);
    const Student& result() const noexcept { return result_; }
    static void onOk(Fl_Widget*, void* data);
    void collect();
};

class ShelfDialog : public DialogBase {
    Fl_Input* code_;
    Fl_Input* location_;
    Fl_Input* category_;
    Fl_Int_Input* capacity_;
    Fl_Input* remark_;
    Bookshelf result_;

public:
    explicit ShelfDialog(const Bookshelf& initial);
    const Bookshelf& result() const noexcept { return result_; }
    static void onOk(Fl_Widget*, void* data);
    void collect();
};

class BookPanel : public Fl_Group {
protected:
    LibraryService& svc_;
    MainWindow* owner_;
    Fl_Input* search_;
    Fl_Hold_Browser* list_;
    Fl_Button* btnSearch_;
    Fl_Button* btnAdd_;
    Fl_Button* btnEdit_;
    Fl_Button* btnDel_;
    Fl_Button* btnRefresh_;
    int widths_[8];
    std::vector<Book> items_;

public:
    BookPanel(int x, int y, int w, int h, LibraryService& svc, MainWindow* owner);
    void refresh(const std::string& keyword = "");
    void status(const std::string& message);
    void doAdd();
    void doEdit();
    void doDel();
    static void onSearch(Fl_Widget*, void* data);
    static void onAdd(Fl_Widget*, void* data);
    static void onEdit(Fl_Widget*, void* data);
    static void onDel(Fl_Widget*, void* data);
    static void onRefresh(Fl_Widget*, void* data);
};

class StudentPanel : public Fl_Group {
protected:
    LibraryService& svc_;
    MainWindow* owner_;
    Fl_Hold_Browser* list_;
    Fl_Button* btnAdd_;
    Fl_Button* btnEdit_;
    Fl_Button* btnDel_;
    Fl_Button* btnRefresh_;
    int widths_[9];
    std::vector<Student> items_;

public:
    StudentPanel(int x, int y, int w, int h, LibraryService& svc, MainWindow* owner);
    void refresh();
    void status(const std::string& message);
    void doAdd();
    void doEdit();
    void doDel();
    static void onAdd(Fl_Widget*, void* data);
    static void onEdit(Fl_Widget*, void* data);
    static void onDel(Fl_Widget*, void* data);
    static void onRefresh(Fl_Widget*, void* data);
};

class ShelfPanel : public Fl_Group {
protected:
    LibraryService& svc_;
    MainWindow* owner_;
    Fl_Hold_Browser* list_;
    Fl_Button* btnAdd_;
    Fl_Button* btnEdit_;
    Fl_Button* btnDel_;
    Fl_Button* btnRefresh_;
    int widths_[8];
    std::vector<ShelfStat> items_;

public:
    ShelfPanel(int x, int y, int w, int h, LibraryService& svc, MainWindow* owner);
    void refresh();
    void status(const std::string& message);
    void doAdd();
    void doEdit();
    void doDel();
    static void onAdd(Fl_Widget*, void* data);
    static void onEdit(Fl_Widget*, void* data);
    static void onDel(Fl_Widget*, void* data);
    static void onRefresh(Fl_Widget*, void* data);
};

class LoanPanel : public Fl_Group {
protected:
    LibraryService& svc_;
    MainWindow* owner_;
    Fl_Choice* studentChoice_;
    Fl_Choice* bookChoice_;
    Fl_Choice* filterChoice_;
    Fl_Button* btnBorrow_;
    Fl_Button* btnReturn_;
    Fl_Button* btnRefresh_;
    Fl_Hold_Browser* list_;
    int widths_[9];
    std::vector<int> studentIds_;
    std::vector<int> bookIds_;
    std::vector<int> recordIds_;
    std::vector<std::string> labels_;
    std::vector<LoanDetail> items_;

public:
    LoanPanel(int x, int y, int w, int h, LibraryService& svc, MainWindow* owner);
    void refreshChoices();
    void refresh();
    void status(const std::string& message);
    void doBorrow();
    void doReturn();
    static void onBorrow(Fl_Widget*, void* data);
    static void onReturn(Fl_Widget*, void* data);
    static void onFilter(Fl_Widget*, void* data);
    static void onRefresh(Fl_Widget*, void* data);
};

class MainWindow : public Fl_Double_Window {
    LibraryService& svc_;
    Fl_Box* topBar_;
    Fl_Box* statusBar_;
    Fl_Button* btnRefresh_;
    Fl_Button* btnAbout_;
    Fl_Tabs* tabs_;
    BookPanel* bookPanel_;
    StudentPanel* studentPanel_;
    ShelfPanel* shelfPanel_;
    LoanPanel* loanPanel_;

public:
    MainWindow(int w, int h, const char* title, LibraryService& svc);
    void setStatus(const std::string& message);
    void refreshAll();
    static void onRefresh(Fl_Widget*, void* data);
    static void onAbout(Fl_Widget*, void* data);
};

}  // namespace lls
