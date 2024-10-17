/////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/clrpicker.h
// Purpose:     wxColourButton header
// Author:      Francesco Montorsi
// Created:     14/4/2006
// Copyright:   (c) Francesco Montorsi
// Licence:     wxWindows Licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTK_CLRPICKER_H_
#define _WX_GTK_CLRPICKER_H_

#include "wx/button.h"

//-----------------------------------------------------------------------------
// wxColourButton
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxColourButton : public wxButton,
                                        public wxColourPickerWidgetBase
{
public:
    wxColourButton() : m_topParent(nullptr) {}
    wxColourButton(wxWindow *parent,
                   wxWindowID id,
                   const wxColour& initial = *wxBLACK,
                   const wxPoint& pos = wxDefaultPosition,
                   const wxSize& size = wxDefaultSize,
                   long style = wxCLRBTN_DEFAULT_STYLE,
                   const wxValidator& validator = wxDefaultValidator,
                   const wxString& name = wxASCII_STR(wxColourPickerWidgetNameStr))
        : m_topParent(nullptr)
    {
        Create(parent, id, initial, pos, size, style, validator, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxColour& initial = *wxBLACK,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxCLRBTN_DEFAULT_STYLE,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxColourPickerWidgetNameStr));

    virtual ~wxColourButton();

protected:
    void UpdateColour() override;

public:     // used by the GTK callback only

    void GTKSetColour(const wxColour& colour)
        { m_colour = colour; }

    wxWindow *m_topParent;

private:
    wxDECLARE_DYNAMIC_CLASS(wxColourButton);
};

#endif // _WX_GTK_CLRPICKER_H_

