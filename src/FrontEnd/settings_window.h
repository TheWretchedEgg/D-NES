/* All Settings window */

#pragma once

#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/notebook.h>

class SettingsWindow : public wxDialog
{
public:
    SettingsWindow();
    void SaveSettings();

private:
    wxNotebook* notebook;
    wxTextCtrl* romDirectory;
    wxButton* directorySelect;
    wxButton* ok;
    wxButton* cancel;

    void PopulateFields();
    void OnDirectorySelect(wxCommandEvent& event);;
};

const int ID_DIR_SELECT = 200;
