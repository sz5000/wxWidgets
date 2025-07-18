///////////////////////////////////////////////////////////////////////////////
// Name:        src/generic/spinctlg.cpp
// Purpose:     implements wxSpinCtrl as a composite control
// Author:      Vadim Zeitlin
// Created:     29.01.01
// Copyright:   (c) 2001 Vadim Zeitlin <zeitlin@dptmaths.ens-cachan.fr>
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"


#ifndef WX_PRECOMP
    #include "wx/textctrl.h"
#endif //WX_PRECOMP

#include "wx/spinctrl.h"
#include "wx/tooltip.h"

#if wxUSE_SPINCTRL

#include "wx/private/spinctrl.h"

wxIMPLEMENT_DYNAMIC_CLASS(wxSpinDoubleEvent, wxNotifyEvent);

// There are port-specific versions for the wxSpinCtrl, so exclude the
// contents of this file in those cases
#if !defined(wxHAS_NATIVE_SPINCTRL) || !defined(wxHAS_NATIVE_SPINCTRLDOUBLE)

#include "wx/spinbutt.h"

#if wxUSE_SPINBTN

#include "wx/numformatter.h"
#include "wx/valnum.h"
#include "wx/valtext.h"

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// The margin between the text control and the spin: the value here is the same
// as the margin between the spin button and its "buddy" text control in wxMSW
// so the generic control looks similarly to the native one there, we might
// need to use different value for the other platforms (and maybe even
// determine it dynamically?).
static const wxCoord MARGIN = 1;

#define SPINCTRLBUT_MAX 32000 // large to avoid wrap around trouble

// ----------------------------------------------------------------------------
// wxSpinCtrlTextGeneric: text control used by spin control
// ----------------------------------------------------------------------------

class wxSpinCtrlTextGeneric : public wxTextCtrl
{
public:
    wxSpinCtrlTextGeneric(wxSpinCtrlGenericBase *spin, const wxString& value, long style=0)
        : wxTextCtrl(spin, wxID_ANY, value, wxDefaultPosition, wxDefaultSize,
                     // This is tricky: we want to honour all alignment flags
                     // except wxALIGN_CENTER_VERTICAL because it's the same
                     // as wxTE_PASSWORD and we definitely don't want to show
                     // asterisks in spin control. We also want to respect
                     // wxTE_PROCESS and the border flags as well.
                     style & (wxALIGN_MASK | wxBORDER_MASK | wxTE_PROCESS_ENTER)
                           & ~wxTE_PASSWORD)
    {
        m_spin = spin;

        InvalidateBestSize();

        // remove the default minsize, the spinctrl will have one instead
        SetSizeHints(wxDefaultCoord, wxDefaultCoord);
    }

    virtual ~wxSpinCtrlTextGeneric()
    {
        // MSW sends extra kill focus event on destroy
        if (m_spin)
            m_spin->m_textCtrl = nullptr;

        m_spin = nullptr;
    }

    void OnChar( wxKeyEvent &event )
    {
        if ( !m_spin->ProcessWindowEvent(event) )
            event.Skip();
    }

    void OnTextEvent(wxCommandEvent& event)
    {
        wxCommandEvent eventCopy(event);
        eventCopy.SetEventObject(m_spin);
        eventCopy.SetId(m_spin->GetId());
        eventCopy.SetInt(wxAtoi(event.GetString()));
        m_spin->ProcessWindowEvent(eventCopy);
    }

    void OnKillFocus(wxFocusEvent& event)
    {
        if (m_spin)
            m_spin->ProcessWindowEvent(event);

        event.Skip();
    }

    virtual wxSize DoGetBestSize() const override
    {
        wxString minVal = m_spin->DoValueToText(m_spin->m_min);
        wxString maxVal = m_spin->DoValueToText(m_spin->m_max);
        wxSize minValSize = GetSizeFromText(minVal);
        wxSize maxValSize = GetSizeFromText(maxVal);

        return wxSize(wxMax(minValSize.x, maxValSize.x), wxMax(minValSize.y, maxValSize.y));
    }

    wxSpinCtrlGenericBase *m_spin;

private:
    wxDECLARE_EVENT_TABLE();
};

wxBEGIN_EVENT_TABLE(wxSpinCtrlTextGeneric, wxTextCtrl)
    EVT_CHAR(wxSpinCtrlTextGeneric::OnChar)

    // Forward the text events to wxSpinCtrl itself adjusting them slightly in
    // the process.
    EVT_TEXT(wxID_ANY, wxSpinCtrlTextGeneric::OnTextEvent)

    // And we need to forward this one too as wxSpinCtrl is supposed to
    // generate it if wxTE_PROCESS_ENTER is used with it (and if it isn't,
    // we're never going to get EVT_TEXT_ENTER in the first place).
    EVT_TEXT_ENTER(wxID_ANY, wxSpinCtrlTextGeneric::OnTextEvent)

    EVT_KILL_FOCUS(wxSpinCtrlTextGeneric::OnKillFocus)
wxEND_EVENT_TABLE()

// ----------------------------------------------------------------------------
// wxSpinCtrlButtonGeneric: spin button used by spin control
// ----------------------------------------------------------------------------

class wxSpinCtrlButtonGeneric : public wxSpinButton
{
public:
    wxSpinCtrlButtonGeneric(wxSpinCtrlGenericBase *spin, int style)
        : wxSpinButton(spin, wxID_ANY, wxDefaultPosition,
                       wxDefaultSize, style | wxSP_VERTICAL)
    {
        m_spin = spin;

        SetRange(-SPINCTRLBUT_MAX, SPINCTRLBUT_MAX);

        // remove the default minsize, the spinctrl will have one instead
        SetSizeHints(wxDefaultCoord, wxDefaultCoord);
    }

    void OnSpinButton(wxSpinEvent& event)
    {
        if (m_spin)
            m_spin->OnSpinButton(event);
    }

    wxSpinCtrlGenericBase *m_spin;

private:
    wxDECLARE_EVENT_TABLE();
};

wxBEGIN_EVENT_TABLE(wxSpinCtrlButtonGeneric, wxSpinButton)
    EVT_SPIN_UP(  wxID_ANY, wxSpinCtrlButtonGeneric::OnSpinButton)
    EVT_SPIN_DOWN(wxID_ANY, wxSpinCtrlButtonGeneric::OnSpinButton)
wxEND_EVENT_TABLE()

// ============================================================================
// wxSpinCtrlGenericBase
// ============================================================================

// ----------------------------------------------------------------------------
// wxSpinCtrlGenericBase creation
// ----------------------------------------------------------------------------

void wxSpinCtrlGenericBase::Init()
{
    m_value         = 0;
    m_min           = 0;
    m_max           = 100;
    m_increment     = 1;
    m_snap_to_ticks = false;

    m_spin_value    = 0;

    m_textCtrl = nullptr;
    m_spinButton  = nullptr;
}

bool wxSpinCtrlGenericBase::Create(wxWindow *parent,
                                   wxWindowID id,
                                   const wxString& value,
                                   const wxPoint& pos, const wxSize& size,
                                   long style,
                                   double min, double max, double initial,
                                   double increment,
                                   const wxString& name)
{
    // don't use borders for this control itself, it wouldn't look good with
    // the text control borders (but we might want to use style border bits to
    // select the text control style)
    if ( !wxControl::Create(parent, id, wxDefaultPosition, wxDefaultSize,
                            (style & ~wxBORDER_MASK) | wxBORDER_NONE,
                            wxDefaultValidator, name) )
    {
        return false;
    }

    m_min = min;
    m_max = max;
    m_increment = increment;

    // Note that AdjustAndSnap() uses the variables set above, so only call it
    // after assigning the values to them.
    m_value = AdjustAndSnap(initial);

    // the string value overrides the numeric one (for backwards compatibility
    // reasons and also because it is simpler to specify the string value which
    // comes much sooner in the list of arguments and leave the initial
    // parameter unspecified)
    if ( !value.empty() )
    {
        double d;
        if ( DoTextToValue(value, &d) )
            m_value = AdjustAndSnap(d);
    }

    m_textCtrl   = new wxSpinCtrlTextGeneric(this, DoValueToText(m_value), style);
    m_spinButton = new wxSpinCtrlButtonGeneric(this, style);

#if wxUSE_TOOLTIPS
    m_textCtrl->SetToolTip(GetToolTipText());
    m_spinButton->SetToolTip(GetToolTipText());
#endif // wxUSE_TOOLTIPS

    ResetTextValidator();

    m_spin_value = m_spinButton->GetValue();

    SetInitialSize(size);
    Move(pos);

    return true;
}

wxSpinCtrlGenericBase::~wxSpinCtrlGenericBase()
{
    // delete the controls now, don't leave them alive even though they would
    // still be eventually deleted by our parent - but it will be too late, the
    // user code expects them to be gone now

    if (m_textCtrl)
    {
        // null this since MSW sends KILL_FOCUS on deletion, see ~wxSpinCtrlTextGeneric
        wxDynamicCast(m_textCtrl, wxSpinCtrlTextGeneric)->m_spin = nullptr;

        wxSpinCtrlTextGeneric *text = (wxSpinCtrlTextGeneric*)m_textCtrl;
        m_textCtrl = nullptr;
        delete text;
    }

    wxDELETE(m_spinButton);
}

wxWindowList wxSpinCtrlGenericBase::GetCompositeWindowParts() const
{
    wxWindowList parts;
    parts.push_back(m_textCtrl);
    parts.push_back(m_spinButton);
    return parts;
}

// ----------------------------------------------------------------------------
// geometry
// ----------------------------------------------------------------------------

wxSize wxSpinCtrlGenericBase::DoGetBestSize() const
{
    return DoGetSizeFromTextSize(m_textCtrl->GetBestSize().x, -1);
}

wxSize wxSpinCtrlGenericBase::DoGetSizeFromTextSize(int xlen, int ylen) const
{
    const wxSize sizeBtn = m_spinButton->GetBestSize();
    const wxSize sizeText = m_textCtrl->GetSizeFromTextSize(xlen, ylen);

    // Note that we don't use the button height here, as it can be
    // much greater than that of a text control that we want to resemble.
    return wxSize(sizeText.x + sizeBtn.x + MARGIN, sizeText.y);
}

void wxSpinCtrlGenericBase::DoMoveWindow(int x, int y, int width, int height)
{
    wxControl::DoMoveWindow(x, y, width, height);

    // position the subcontrols inside the client area

    // Use GetBestSize instead of GetSize to get the size of the spin control.
    // This fixes a problem on wxMSW when the size is set after a DPI change.
    // GetSize returns the old, invalid, size. GetBestSize will return the size
    // that the control should be. Normally, GetBestSize and GetSize should
    // always return the same value because the size of the spinButton never
    // changes.
    wxSize sizeBtn = m_spinButton->GetBestSize();

    wxCoord wText = width - sizeBtn.x - MARGIN;
    m_textCtrl->SetSize(0, 0, wText, height);
    m_spinButton->SetSize(0 + wText + MARGIN, 0, wxDefaultCoord, height);
}

// ----------------------------------------------------------------------------
// operations forwarded to the subcontrols
// ----------------------------------------------------------------------------

void wxSpinCtrlGenericBase::SetFocus()
{
    if ( m_textCtrl )
        m_textCtrl->SetFocus();
}

#ifdef __WXMSW__

void wxSpinCtrlGenericBase::DoEnable(bool enable)
{
     wxSpinCtrlBase::DoEnable(enable);
}

#endif // __WXMSW__

bool wxSpinCtrlGenericBase::Enable(bool enable)
{
    if ( !wxSpinCtrlBase::Enable(enable) )
        return false;

    m_spinButton->Enable(enable);
    m_textCtrl->Enable(enable);

    return true;
}

bool wxSpinCtrlGenericBase::Show(bool show)
{
    if ( !wxControl::Show(show) )
        return false;

    // under GTK Show() is called the first time before we are fully
    // constructed
    if ( m_spinButton )
    {
        m_spinButton->Show(show);
        m_textCtrl->Show(show);
    }

    return true;
}


bool wxSpinCtrlGenericBase::SetBackgroundColour(const wxColour& colour)
{
    // We need to provide this otherwise the entire composite window
    // background and therefore the between component spaces
    // will be changed.
    if ( m_textCtrl )
        return m_textCtrl->SetBackgroundColour(colour);

    return true;
}

// ----------------------------------------------------------------------------
// Handle sub controls events
// ----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(wxSpinCtrlGenericBase, wxSpinCtrlBase)
    EVT_CHAR(wxSpinCtrlGenericBase::OnTextChar)
    EVT_KILL_FOCUS(wxSpinCtrlGenericBase::OnTextLostFocus)
    EVT_MOUSEWHEEL(wxSpinCtrlGenericBase::OnMouseWheel)
wxEND_EVENT_TABLE()

void wxSpinCtrlGenericBase::OnSpinButton(wxSpinEvent& event)
{
    event.Skip();

    // Pressing the spin button should also give the focus to the text part of
    // the control, at least this is how the native control behaves under MSW.
    SetFocus();

    // Sync the textctrl since the user expects that the button will modify
    // what they see in the textctrl.
    SyncSpinToText(SendEvent_None);

    int spin_value = event.GetPosition();
    double step = (event.GetEventType() == wxEVT_SCROLL_LINEUP) ? 1 : -1;

    // Use the spinbutton's acceleration, if any, but not if wrapping around
    if (((spin_value >= 0) && (m_spin_value >= 0)) || ((spin_value <= 0) && (m_spin_value <= 0)))
        step *= abs(spin_value - m_spin_value);

    double value = AdjustToFitInRange(m_value + step*m_increment);

    // Ignore the edges when it wraps since the up/down event may be opposite
    // They are in GTK and Mac
    if (abs(spin_value - m_spin_value) > SPINCTRLBUT_MAX)
    {
        m_spin_value = spin_value;
        return;
    }

    m_spin_value = spin_value;

    // Notify about the change in wxTextCtrl too.
    if ( DoSetValue(value, SendEvent_Text) )
        DoSendEvent();
}

void wxSpinCtrlGenericBase::OnTextLostFocus(wxFocusEvent& event)
{
    if ( SyncSpinToText(SendEvent_Text) )
        DoSendEvent();

    event.Skip();
}

void wxSpinCtrlGenericBase::OnTextChar(wxKeyEvent& event)
{
    if ( !HasFlag(wxSP_ARROW_KEYS) )
    {
        event.Skip();
        return;
    }

    double value = m_value;
    switch ( event.GetKeyCode() )
    {
        case WXK_UP :
            value += m_increment;
            break;

        case WXK_DOWN :
            value -= m_increment;
            break;

        case WXK_PAGEUP :
            value += m_increment * 10.0;
            break;

        case WXK_PAGEDOWN :
            value -= m_increment * 10.0;
            break;

        default:
            event.Skip();
            return;
    }

    value = AdjustToFitInRange(value);

    SyncSpinToText(SendEvent_None);

    // No need to send event, it was already generated by wxTextCtrl itself.
    if ( DoSetValue(value, SendEvent_None) )
        DoSendEvent();
}

void wxSpinCtrlGenericBase::OnMouseWheel(wxMouseEvent& event)
{
    if ( event.GetWheelAxis() != wxMOUSE_WHEEL_VERTICAL || event.GetWheelRotation() == 0 )
    {
        event.Skip();
        return;
    }

    double value = m_value;

    if( event.GetWheelRotation() > 0 )
    {
        value += m_increment;
    }
    else if ( event.GetWheelRotation() < 0 )
    {
        value -= m_increment;
    }

    value = AdjustToFitInRange(value);

    SyncSpinToText(SendEvent_None);

    // No need to send event, it was already generated by wxTextCtrl itself.
    if ( DoSetValue(value, SendEvent_None) )
        DoSendEvent();
}

// ----------------------------------------------------------------------------
// Textctrl functions
// ----------------------------------------------------------------------------

bool wxSpinCtrlGenericBase::SyncSpinToText(SendEvent sendEvent)
{
    if ( !m_textCtrl || !m_textCtrl->IsModified() )
        return false;

    double textValue;
    if ( DoTextToValue(m_textCtrl->GetValue(), &textValue) )
    {
        if (textValue > m_max)
            textValue = m_max;
        else if (textValue < m_min)
            textValue = m_min;
    }
    else // text contents is not a valid number at all
    {
        // replace its contents with the last valid value
        textValue = m_value;
    }

    // we must always set the value here, even if it's equal to m_value, as
    // otherwise we could be left with an out of range value when leaving the
    // text control and the current value is already m_max for example
    return DoSetValue(textValue, sendEvent);
}

// ----------------------------------------------------------------------------
// changing value and range
// ----------------------------------------------------------------------------

wxString wxSpinCtrlGenericBase::GetTextValue() const
{
    return m_textCtrl ? m_textCtrl->GetValue() : wxString();
}

void wxSpinCtrlGenericBase::SetValue(const wxString& text)
{
    wxCHECK_RET( m_textCtrl, wxT("invalid call to wxSpinCtrl::SetValue") );

    double val;
    if ( DoTextToValue(text, &val) && InRange(val) )
    {
        DoSetValue(val, SendEvent_None);
    }
    else // not a number at all or out of range
    {
        m_value = m_min;

        m_textCtrl->ChangeValue(text);
        m_textCtrl->SelectAll();
    }
}

double wxSpinCtrlGenericBase::AdjustAndSnap(double val) const
{
    if ( val < m_min )
        val = m_min;
    if ( val > m_max )
        val = m_max;

    if ( m_snap_to_ticks && (m_increment != 0) )
    {
        double snap_value = val / m_increment;

        if (wxFinite(snap_value)) // FIXME what to do about a failure?
        {
            if ((snap_value - floor(snap_value)) < (ceil(snap_value) - snap_value))
                val = floor(snap_value) * m_increment;
            else
                val = ceil(snap_value) * m_increment;
        }
    }

    return val;
}

bool wxSpinCtrlGenericBase::DoSetValue(double val, SendEvent sendEvent)
{
    wxCHECK_MSG( m_textCtrl, false, wxT("invalid call to wxSpinCtrl::SetValue") );

    val = AdjustAndSnap(val);

    wxString str(DoValueToText(val));

    if ((val != m_value) || (str != m_textCtrl->GetValue()))
    {
        if ( !DoTextToValue(str, &m_value ) )    // wysiwyg for textctrl
            m_value = val;

        switch ( sendEvent )
        {
            case SendEvent_None:
                m_textCtrl->ChangeValue(str);
                break;

            case SendEvent_Text:
                m_textCtrl->SetValue(str);
                break;
        }

        m_textCtrl->SelectAll();
        m_textCtrl->DiscardEdits();
        return true;
    }

    return false;
}

double wxSpinCtrlGenericBase::AdjustToFitInRange(double value) const
{
    if (value < m_min)
        value = HasFlag(wxSP_WRAP) ? m_max : m_min;
    if (value > m_max)
        value = HasFlag(wxSP_WRAP) ? m_min : m_max;

    return value;
}

void wxSpinCtrlGenericBase::DoSetRange(double min, double max)
{
    // Negative values in the range are allowed only if base == 10
    if ( !wxSpinCtrlImpl::IsBaseCompatibleWithRange(min, max, GetBase()) )
    {
        return;
    }

    if ( min != m_min || max != m_max )
        m_textCtrl->InvalidateBestSize();

    m_min = min;
    if ( m_value < m_min )
        DoSetValue(m_min, SendEvent_None);
    m_max = max;
    if ( m_value > m_max )
        DoSetValue(m_max, SendEvent_None);

    ResetTextValidator();
}

void wxSpinCtrlGenericBase::DoSetIncrement(double inc)
{
    m_increment = inc;
}

void wxSpinCtrlGenericBase::SetSnapToTicks(bool snap_to_ticks)
{
    m_snap_to_ticks = snap_to_ticks;
    DoSetValue(m_value, SendEvent_None);
}

void wxSpinCtrlGenericBase::SetSelection(long from, long to)
{
    wxCHECK_RET( m_textCtrl, wxT("invalid call to wxSpinCtrl::SetSelection") );

    m_textCtrl->SetSelection(from, to);
}

#ifndef wxHAS_NATIVE_SPINCTRL

//-----------------------------------------------------------------------------
// wxSpinCtrl
//-----------------------------------------------------------------------------

bool wxSpinCtrl::SetBase(int base)
{
    // Currently we only support base 10 and 16. We could add support for base
    // 8 quite easily but wxMSW doesn't support it natively so don't bother.
    if ( base != 10 && base != 16 )
        return false;

    if ( base == m_base )
        return true;

    // For negative values in the range only base == 10 is allowed
    if ( !wxSpinCtrlImpl::IsBaseCompatibleWithRange(m_min, m_max, base) )
        return false;

    // Update the current control contents to show in the new base: be careful
    // to call DoTextToValue() before changing the base...
    double val;
    const bool hasValidVal = DoTextToValue(m_textCtrl->GetValue(), &val);

    m_base = base;

    m_textCtrl->InvalidateBestSize();
    ResetTextValidator();

    // ... but DoValueToText() after doing it.
    if ( hasValidVal )
        m_textCtrl->ChangeValue(DoValueToText(val));

    return true;
}

void wxSpinCtrl::DoSendEvent()
{
    wxSpinEvent event( wxEVT_SPINCTRL, GetId());
    event.SetEventObject( this );
    event.SetPosition(GetValue());
    event.SetString(m_textCtrl->GetValue());
    GetEventHandler()->ProcessEvent( event );
}

bool wxSpinCtrl::DoTextToValue(const wxString& text, double *val)
{
    long lval;
    if ( !text.ToLong(&lval, GetBase()) )
        return false;

    *val = static_cast<double>(lval);

    return true;
}

wxString wxSpinCtrl::DoValueToText(double val)
{
    switch ( GetBase() )
    {
        case 16:
            return wxSpinCtrlImpl::FormatAsHex(static_cast<long>(val), GetMax());

        default:
            wxFAIL_MSG( wxS("Unsupported spin control base") );
            wxFALLTHROUGH;

        case 10:
            return wxString::Format("%ld", static_cast<long>(val));
    }
}

void wxSpinCtrl::ResetTextValidator()
{
#if wxUSE_VALIDATORS
    if ( GetBase() == 10 )
    {
        wxIntegerValidator<int> validator;
        validator.SetRange(GetMin(), GetMax());
        m_textCtrl->SetValidator(validator);
    }
    else // == 16
    {
        wxTextValidator validator(wxFILTER_XDIGITS);
        m_textCtrl->SetValidator(validator);

    }
#endif // wxUSE_VALIDATORS
}

#endif // !wxHAS_NATIVE_SPINCTRL

//-----------------------------------------------------------------------------
// wxSpinCtrlDouble
//-----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(wxSpinCtrlDouble, wxSpinCtrlGenericBase);

bool
wxSpinCtrlDouble::Create(wxWindow *parent,
                         wxWindowID id,
                         const wxString& value,
                         const wxPoint& pos,
                         const wxSize& size,
                         long style,
                         double min, double max, double initial,
                         double inc,
                         const wxString& name)
{
    DoSetDigits(wxSpinCtrlImpl::DetermineDigits(inc));

    return wxSpinCtrlGenericBase::Create(parent, id, value, pos, size,
                                         style, min, max, initial,
                                         inc, name);
}

void wxSpinCtrlDouble::DoSendEvent()
{
    wxSpinDoubleEvent event( wxEVT_SPINCTRLDOUBLE, GetId());
    event.SetEventObject( this );
    event.SetValue(m_value);
    event.SetString(m_textCtrl->GetValue());
    GetEventHandler()->ProcessEvent( event );
}

bool wxSpinCtrlDouble::DoTextToValue(const wxString& text, double *val)
{
    return wxNumberFormatter::FromString(text, val);
}

wxString wxSpinCtrlDouble::DoValueToText(double val)
{
    return wxNumberFormatter::ToString(val, m_digits);
}

void wxSpinCtrlDouble::SetIncrement(double inc)
{
    if ( inc == m_increment )
        return;

    DoSetIncrement(inc);

    const unsigned digits = wxSpinCtrlImpl::DetermineDigits(inc);

    // We don't decrease the number of digits here, as this is unnecessary and
    // could be undesirable, but we do increase it if the current number is not
    // high enough to show the numbers without losing precision.
    if ( digits > m_digits )
        DoSetDigitsAndUpdate(digits);
}

void wxSpinCtrlDouble::SetDigits(unsigned digits)
{
    wxCHECK_RET( digits <= wxSpinCtrlImpl::SPINCTRLDBL_MAX_DIGITS,
                 "too many digits for wxSpinCtrlDouble" );

    if ( digits == m_digits )
        return;

    DoSetDigitsAndUpdate(digits);
}

void wxSpinCtrlDouble::DoSetDigitsAndUpdate(unsigned digits)
{
    DoSetDigits(digits);

    ResetTextValidator();
    m_textCtrl->InvalidateBestSize();

    DoSetValue(m_value, SendEvent_None);
}

void wxSpinCtrlDouble::DoSetDigits(unsigned digits)
{
    m_digits = digits;
}

void wxSpinCtrlDouble::ResetTextValidator()
{
#if wxUSE_VALIDATORS
    wxFloatingPointValidator<double> validator(m_digits);
    validator.SetRange(m_min, m_max);
    m_textCtrl->SetValidator(validator);
#endif // wxUSE_VALIDATORS
}

#endif // wxUSE_SPINBTN

#endif // !wxPort-with-native-spinctrl

#endif // wxUSE_SPINCTRL
