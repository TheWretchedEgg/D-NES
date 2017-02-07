#pragma once

#include <wx/dialog.h>

class NES;
class MainWindow;
class wxSlider;
class wxCheckBox;
class wxStaticText;

wxDECLARE_EVENT(EVT_AUDIO_WINDOW_CLOSED, wxCommandEvent);

class AudioSettingsWindow : public wxDialog
{
public:
    AudioSettingsWindow(MainWindow* parentWindow);
    void SetNes(NES* nes);

private:
    void OnClose(wxCloseEvent& event);
    void OnOk(wxCommandEvent& event);
    void OnCancel(wxCommandEvent& event);

    void EnableAudioClicked(wxCommandEvent& event);
    void EnableFiltersClicked(wxCommandEvent& event);
    void MasterVolumeChanged(wxCommandEvent& event);
    void PulseOneVolumeChanged(wxCommandEvent& event);
    void PulseTwoVolumeChanged(wxCommandEvent& event);
    void TriangleVolumeChanged(wxCommandEvent& event);
    void NoiseVolumeChanged(wxCommandEvent& event);
    void DmcVolumeChanged(wxCommandEvent& event);

    wxSlider* MasterVolume;
    wxSlider* PulseOneVolume;
    wxSlider* PulseTwoVolume;
    wxSlider* TriangleVolume;
    wxSlider* NoiseVolume;
    wxSlider* DmcVolume;
    wxStaticText* CurrentMasterVolume;
    wxStaticText* CurrentPulseOneVolume;
    wxStaticText* CurrentPulseTwoVolume;
    wxStaticText* CurrentTriangleVolume;
    wxStaticText* CurrentNoiseVolume;
    wxStaticText* CurrentDmcVolume;

    wxCheckBox* EnableAudioCheckBox;
    wxCheckBox* EnableFiltersCheckBox;

    NES* Nes;
};

const int ID_AUDIO_ENABLED = 200;
const int ID_FILTERS_ENABLED = 201;
const int ID_MASTER_SLIDER = 202;
const int ID_PULSE_ONE_SLIDER = 203;
const int ID_PULSE_TWO_SLIDER = 204;
const int ID_TRIANGLE_SLIDER = 205;
const int ID_NOISE_SLIDER = 206;
const int ID_DMC_SLIDER = 207;