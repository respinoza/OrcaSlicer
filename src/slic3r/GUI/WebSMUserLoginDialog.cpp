#include "WebSMUserLoginDialog.hpp"

#include <string.h>
#include "I18N.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/Utils/SnapmakerAccount.hpp"
#include "slic3r/Utils/SnapLogClient.hpp"
#include "common_func/common_func.hpp"
#include "slic3r/GUI/Widgets/StateColor.hpp"
#include "Widgets/Button.hpp"

#include <boost/format.hpp>

#include <wx/sizer.h>
#include <wx/toolbar.h>
#include <wx/textdlg.h>

#include <wx/wx.h>
#include <wx/fileconf.h>
#include <wx/file.h>
#include <wx/wfstream.h>
#include <wx/weakref.h>

#include <boost/cast.hpp>
#include <boost/lexical_cast.hpp>

#include <nlohmann/json.hpp>
#include "MainFrame.hpp"
#include <boost/dll.hpp>

#include <sstream>
#include <slic3r/GUI/Widgets/WebView.hpp>
#include "sentry_wrapper/SentryWrapper.hpp"
using namespace std;

using namespace nlohmann;

namespace Slic3r { namespace GUI {

#define NETWORK_OFFLINE_TIMER_ID 10001
#define CALLBACK_POLL_TIMER_ID   10002
// Was 10002 before the 2.4.0 merge; moved so it does not collide with upstream's
// CALLBACK_POLL_TIMER_ID (a shared id would route both timers to one handler).
#define SILENT_REAUTH_TIMEOUT_TIMER_ID 10003

BEGIN_EVENT_TABLE(SMUserLogin, wxDialog)
EVT_TIMER(NETWORK_OFFLINE_TIMER_ID, SMUserLogin::OnTimer)
EVT_TIMER(CALLBACK_POLL_TIMER_ID, SMUserLogin::OnCallbackPollTimer)
EVT_TIMER(SILENT_REAUTH_TIMEOUT_TIMER_ID, SMUserLogin::OnSilentTimeout)
END_EVENT_TABLE()

int SMUserLogin::web_sequence_id = 20000;

SMUserLogin::SMUserLogin(bool isLogout) : wxDialog((wxWindow *) (wxGetApp().mainframe), wxID_ANY, "Snapmaker Orca")
{
    // url
    auto region = wxGetApp().app_config->get_country_code();
    if (region.find("CN") == std::string::npos) {
        TargetUrl     = "https://id.snapmaker.com?from=orca";
        LogoutUrl     = "https://id.snapmaker.com/logout?from=orca";
        m_hostUrl     = "https://id.snapmaker.com";
        m_accountUrl  = "https://id.snapmaker.com";
        m_userInfoUrl = "https://id.snapmaker.com/api/common/accounts/current";
        m_home_url    = "https://www.snapmaker.com/";
    } else {
        TargetUrl     = "https://id.snapmaker.cn?from=orca";
        LogoutUrl     = "https://id.snapmaker.cn/logout?from=orca";
        m_hostUrl     = "https://id.snapmaker.cn";
        m_accountUrl  = "https://api.snapmaker.cn";
        m_userInfoUrl = "https://api.snapmaker.cn/api/common/accounts/current";
        m_home_url    = "https://www.snapmaker.cn/";
    }

    SetBackgroundColour(*wxWHITE);

    BOOST_LOG_TRIVIAL(info) << "login url = " << TargetUrl.ToStdString();

    m_sm_user_agent = wxString::Format("SM-Slicer/v%s", SLIC3R_VERSION);

    // set the frame icon

    // Create the webview
    m_browser = WebView::CreateWebView(this, isLogout ? LogoutUrl : TargetUrl);
    if (m_browser == nullptr) {
        wxLogError("Could not init m_browser");
        return;
    }
    m_browser->Hide();
    m_browser->SetSize(0, 0);

    // Log backend information
    // wxLogMessage(wxWebView::GetBackendVersionInfo().ToString());
    // wxLogMessage("Backend: %s Version: %s",
    // m_browser->GetClassInfo()->GetClassName(),wxWebView::GetBackendVersionInfo().ToString());
    // wxLogMessage("User Agent: %s", m_browser->GetUserAgent());

    // Connect the webview events
    Bind(wxEVT_WEBVIEW_NAVIGATING, &SMUserLogin::OnNavigationRequest, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_NAVIGATED, &SMUserLogin::OnNavigationComplete, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_LOADED, &SMUserLogin::OnDocumentLoaded, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_ERROR, &SMUserLogin::OnError, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_NEWWINDOW, &SMUserLogin::OnNewWindow, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_TITLE_CHANGED, &SMUserLogin::OnTitleChanged, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_FULLSCREEN_CHANGED, &SMUserLogin::OnFullScreenChanged, this, m_browser->GetId());
    //Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &SMUserLogin::OnScriptMessage, this, m_browser->GetId());

    // Connect the idle events
    // Bind(wxEVT_IDLE, &SMUserLogin::OnIdle, this);
    // Bind(wxEVT_CLOSE_WINDOW, &SMUserLogin::OnClose, this);

    // The oauth callback page must be found by polling: WebView2 does not
    // reliably deliver navigation events for that hop, and GUI_App shows this
    // dialog via ShowModal() directly, run() is not used.
    m_callback_timer = new wxTimer(this, CALLBACK_POLL_TIMER_ID);
    m_callback_timer->Start(500);

    // UI
    SetTitle(isLogout ? _L("Log out") : _L("Login"));
    // Set a more sensible size for web browsing
    wxSize pSize = FromDIP(wxSize(650, 840));
    SetSize(pSize);

    int     screenheight = wxSystemSettings::GetMetric(wxSYS_SCREEN_Y, NULL);
    int     screenwidth  = wxSystemSettings::GetMetric(wxSYS_SCREEN_X, NULL);
    int     MaxY         = (screenheight - pSize.y) > 0 ? (screenheight - pSize.y) / 2 : 0;
    wxPoint tmpPT((screenwidth - pSize.x) / 2, MaxY);
    Move(tmpPT);
    wxGetApp().UpdateDlgDarkUI(this);
}

SMUserLogin::~SMUserLogin() {
    if (m_silent_timeout) { m_silent_timeout->Stop(); delete m_silent_timeout; m_silent_timeout = nullptr; }
    if (m_callback_timer != NULL) {
        m_callback_timer->Stop();
        delete m_callback_timer;
        m_callback_timer = NULL;
    }
    if (m_timer != NULL) {
        m_timer->Stop();
        delete m_timer;
        m_timer = NULL;
    }
}

void SMUserLogin::OnCallbackPollTimer(wxTimerEvent &event) {
    try_complete_oauth_callback();
}

// The third-party oauth callback endpoint may answer 200 with the raw token
// json as the page body instead of redirecting to a url containing "token=",
// the only format OnNavigationRequest understands. Detect that page, then
// have a fire-and-forget script smuggle the body out through document.title;
// wxWebView::RunScript is avoided because it busy-pumps the event loop on
// every backend and freezes the app when the web process is dead.
void SMUserLogin::try_complete_oauth_callback() {
    if (m_callback_handled || m_browser == NULL)
        return;
    if (!m_browser->GetCurrentURL().Contains("/api/oauth2/callback/"))
        return;
    if (++m_callback_attempts > 60) { // ~30s at 500ms
        m_callback_timer->Stop();
        return;
    }
    RunScript("document.title='SMOAUTH:'+(document.body?document.body.innerText:'')");
}

void SMUserLogin::OnTimer(wxTimerEvent &event) {
    m_timer->Stop();

    if (m_networkOk == false)
    {
        ShowErrorPage();
    }
}

bool SMUserLogin::run() {
    m_timer = new wxTimer(this, NETWORK_OFFLINE_TIMER_ID);
    m_timer->Start(8000);

    if (this->ShowModal() == wxID_OK) {
        return true;
    } else {
        return false;
    }
}


void SMUserLogin::load_url(wxString &url)
{
    m_browser->LoadURL(url);
    m_browser->SetFocus();
    UpdateState();
}


/**
 * Method that retrieves the current state from the web control and updates
 * the GUI the reflect this current state.
 */
void SMUserLogin::UpdateState()
{
    // SetTitle(m_browser->GetCurrentTitle());
}

void SMUserLogin::OnIdle(wxIdleEvent &WXUNUSED(evt))
{
    if (m_browser->IsBusy()) {
        wxSetCursor(wxCURSOR_ARROWWAIT);
    } else {
        wxSetCursor(wxNullCursor);
    }
}

// void SMUserLogin::OnClose(wxCloseEvent& evt)
//{
//    this->Hide();
//}

/**
 * Callback invoked when there is a request to load a new page (for instance
 * when the user clicks a link)
 */
void SMUserLogin::OnNavigationRequest(wxWebViewEvent &evt)
{
    wxString tmpUrl = evt.GetURL();
    size_t start = tmpUrl.find("token=");
    if (start != std::string::npos) {
        std::string token;
        start += std::string("token=").size();
        // Stop at the next query/fragment delimiter (upstream #807 widened this from "?").
        size_t end = tmpUrl.find_first_of("?&#", start);
        token = (end != std::string::npos) ? tmpUrl.substr(start, end - start).ToStdString()
                                           : tmpUrl.substr(start).ToStdString();
        // Interactive dialog only; the silent re-auth view and upstream's hidden
        // token-refresh dialog are never modal.
        if (!m_silent && this->IsModal())
            this->EndModal(wxID_OK);
        handle_captured_token(token);
    }
    UpdateState();
}

// Shared by the interactive login, the silent re-auth (start_silent) and
// upstream's hidden token refresh (GUI_App::sm_maybe_refresh_login_token).
// The deferred work must not dereference `this` on the non-silent paths:
// GUI_App::sm_on_token_captured()/sm_stop_silent_token_refresh() may delete
// sm_login_dlg (this dialog) while the request is in flight. The silent dialog
// is owned separately and reached only through a weak reference.
void SMUserLogin::handle_captured_token(const std::string& token)
{
    const std::string info_url           = m_userInfoUrl.ToStdString();
    const std::size_t refresh_generation = wxGetApp().sm_token_refresh_generation();
    const bool        silent             = m_silent;
    wxWeakRef<SMUserLogin> self(this);

    wxGetApp().CallAfter([token, info_url, refresh_generation, silent, self]() {
        // Stale capture (logout, or an interactive login took over): drop the token.
        if (wxGetApp().sm_is_token_refresh_current(refresh_generation)) {
            std::string url = info_url;
            auto http = Http::get(url);
            http.header("Authorization", token);
            http.on_complete([&](std::string body, unsigned status) {
                    if (!wxGetApp().sm_is_token_refresh_current(refresh_generation))
                        return;

                    if (status == 200) {
                        json response = json::parse(body, nullptr, false);
                        if (response.is_discarded() || !response.is_object() || !response.contains("data")) {
                            BOOST_LOG_TRIVIAL(error) << "login userinfo response format is invalid"
                                                     << ", body_size=" << body.size();
                            string parse_fail = BP_LOGIN_HTTP_CODE + string(":200 invalid response format");
                            sentryReportLog(SENTRY_LOG_TRACE, parse_fail, BP_LOGIN);
                            return;
                        }
                        SMAccountProfile profile;
                        bool auth_rejected = false;
                        sm_parse_account_response(body, profile, auth_rejected);
                        if (auth_rejected) {
                            BOOST_LOG_TRIVIAL(warning) << "[sm_login] account API rejected a freshly issued token";
                            return;
                        }
                        if (!profile.id.empty())      wxGetApp().sm_get_userinfo()->set_user_id(profile.id);
                        if (!profile.nickname.empty()) wxGetApp().sm_get_userinfo()->set_user_name(profile.nickname);
                        if (!profile.icon.empty())     wxGetApp().sm_get_userinfo()->set_user_icon_url(profile.icon);
                        if (!profile.account.empty())  wxGetApp().sm_get_userinfo()->set_user_account(profile.account);
                        string userInfo = BP_LOGIN_USER_ID + std::string(":") + profile.id;
                        sentryReportLog(SENTRY_LOG_TRACE, userInfo, BP_LOGIN);
                        wxGetApp().sm_get_userinfo()->set_user_token(token);
                        wxGetApp().sm_get_userinfo()->set_user_login(true);
                        wxGetApp().sm_save_login_to_config();
                        wxGetApp().sm_on_token_captured(refresh_generation);
                        // Mirror-push login identity into SnapLogClient (Task 12).
                        ::Slic3r::SnapLog::v1::SnapLogClient::instance().set_user_token(token);
                        ::Slic3r::SnapLog::v1::SnapLogClient::instance().set_user_id(profile.id);
                        auto* ac = wxGetApp().app_config;
                        if (ac) {
                            bool consent = ac->get("app", PRIVACY_POLICY_FLAGS) == "true";
                            ::Slic3r::SnapLog::v1::SnapLogClient::instance().set_consent(consent);
                        }

                        SNAP_LOG_BATCH(Info, "user login success",
                            {"eventName", "user_login_result"}, {"source", "cpp"},
                            {"success", "true"}, {"userId", profile.id});
                    }
                })
                .on_error([&](std::string body, std::string, unsigned status) {
                    if (!wxGetApp().sm_is_token_refresh_current(refresh_generation))
                        return;

                    std::string http_code = BP_LOGIN_HTTP_CODE + string(":") + std::to_string(status) +
                                            ", body_size=" + std::to_string(body.size());
                    sentryReportLog(SENTRY_LOG_TRACE, http_code, BP_LOGIN);
                    SNAP_LOG_BATCH(Error, "user login failed",
                        {"eventName", "user_login_result"}, {"source", "cpp"},
                        {"success", "false"}, {"httpStatus", std::to_string(status)});
                })
                .perform_sync();
        }

        // Silent path: report success iff we are now logged in, then tear down.
        // Reached on every outcome (including a stale capture) so the caller's
        // callback never has to wait for the timeout.
        if (silent && self)
            self->finish_silent(wxGetApp().sm_get_userinfo()->is_user_login());
    });
}

void SMUserLogin::start_silent(std::function<void(bool)> on_done)
{
    m_silent = true;
    m_on_silent_done = std::move(on_done);
    // The constructor already created the webview and began loading TargetUrl;
    // with a live session cookie the redirect carrying token= arrives on its
    // own. Guard with a timeout so a stuck/expired session cannot hang forever.
    m_silent_timeout = new wxTimer(this, SILENT_REAUTH_TIMEOUT_TIMER_ID);
    m_silent_timeout->StartOnce(20000); // 20 s
}

void SMUserLogin::finish_silent(bool ok)
{
    if (m_silent_finished)
        return; // exactly once
    m_silent_finished = true;
    if (m_silent_timeout) {
        m_silent_timeout->Stop();
        delete m_silent_timeout;
        m_silent_timeout = nullptr;
    }
    auto cb = m_on_silent_done;
    m_on_silent_done = nullptr;
    if (cb)
        cb(ok);
    // Destroy the hidden dialog after the callback returns; Destroy() is the
    // wx-safe deferred delete (never `delete this` from an event handler).
    this->Destroy();
}

void SMUserLogin::OnSilentTimeout(wxTimerEvent& /*event*/)
{
    BOOST_LOG_TRIVIAL(info) << "[sm_login] silent re-auth timed out; staying signed out";
    finish_silent(false);
}

/**
 * Callback invoked when a navigation request was accepted
 */
void SMUserLogin::OnNavigationComplete(wxWebViewEvent &evt)
{
    // wxLogMessage("%s", "Navigation complete; url='" + evt.GetURL() + "'");
    m_browser->Show();
    Layout();
    UpdateState();
}

/**
 * Callback invoked when a page is finished loading
 */
void SMUserLogin::OnDocumentLoaded(wxWebViewEvent &evt)
{
    // Only notify if the document is the main frame, not a subframe
    wxString tmpUrl = evt.GetURL();
    std::string strHost = "https://id.snapmaker.com";

    if ( tmpUrl.Contains(strHost) ) {
        m_networkOk = true;
        // wxLogMessage("%s", "Document loaded; url='" + evt.GetURL() + "'");
    }

    try_complete_oauth_callback();
    UpdateState();
}

/**
 * On new window, we veto to stop extra windows appearing
 */
void SMUserLogin::OnNewWindow(wxWebViewEvent &evt)
{
    wxString flag = " (other)";

    if (evt.GetNavigationAction() == wxWEBVIEW_NAV_ACTION_USER) { flag = " (user)"; }

    // wxLogMessage("%s", "New window; url='" + evt.GetURL() + "'" + flag);

    // If we handle new window events then just load them in this window as we
    // are a single window browser
    m_browser->LoadURL(evt.GetURL());

    UpdateState();
}

void SMUserLogin::OnTitleChanged(wxWebViewEvent &evt)
{
    // Body smuggled out by try_complete_oauth_callback(); feed the token to
    // the existing "token=" capture path in OnNavigationRequest.
    const wxString title = evt.GetString();
    if (!title.StartsWith("SMOAUTH:") || m_callback_handled || m_callback_timer == NULL)
        return;

    // No-throw parsing: the title may carry an empty/partial body while the
    // callback page is still rendering; a thrown parse_error would escape this
    // wx event handler and terminate the app (seen crashing on macOS).
    json response = json::parse(into_u8(title.Mid(wxString("SMOAUTH:").Length())), nullptr, false);
    if (response.is_discarded() || !response.is_object() || !response.contains("data"))
        return;
    json data = response["data"];
    if (!data.contains("access_token") || !data["access_token"].is_string())
        return;

    m_callback_handled = true;
    m_callback_timer->Stop();
    m_browser->LoadURL(m_hostUrl + "/?token=" + from_u8(data["access_token"].get<std::string>()));
}

void SMUserLogin::OnFullScreenChanged(wxWebViewEvent &evt)
{
    // wxLogMessage("Full screen changed; status = %d", evt.GetInt());
    ShowFullScreen(evt.GetInt() != 0);
}

void SMUserLogin::OnScriptMessage(wxWebViewEvent &evt)
{
    wxString str_input = evt.GetString();
    try {
        json j = json::parse(into_u8(str_input));

        wxString strCmd = j["command"];

        if (strCmd == "autotest_token")
        {
            m_AutotestToken = j["data"]["token"];
        }
        if (strCmd == "user_login") {
            j["data"]["autotest_token"] = m_AutotestToken;
            Close();
        }
        else if (strCmd == "get_localhost_url") {
            BOOST_LOG_TRIVIAL(info) << "thirdparty_login: get_localhost_url";
            //wxGetApp().start_http_server();
            std::string sequence_id = j["sequence_id"].get<std::string>();
            CallAfter([this, sequence_id] {
                json ack_j;
                ack_j["command"] = "get_localhost_url";
                ack_j["response"]["base_url"] = std::string(LOCALHOST_URL) + std::to_string(LOCALHOST_PORT);
                ack_j["response"]["result"] = "success";
                ack_j["sequence_id"] = sequence_id;
                wxString str_js = wxString::Format("window.postMessage(%s)", ack_j.dump());
                this->RunScript(str_js);
            });
        }
        else if (strCmd == "thirdparty_login") {
            BOOST_LOG_TRIVIAL(info) << "thirdparty_login: thirdparty_login";
            if (j["data"].contains("url")) {
                std::string jump_url = j["data"]["url"].get<std::string>();
                CallAfter([this, jump_url] {
                    wxString url = wxString::FromUTF8(jump_url);
                    wxLaunchDefaultBrowser(url);
                    });
            }
        }
        else if (strCmd == "new_webpage") {
            if (j["data"].contains("url")) {
                std::string jump_url = j["data"]["url"].get<std::string>();
                CallAfter([this, jump_url] {
                    wxString url = wxString::FromUTF8(jump_url);
                    wxLaunchDefaultBrowser(url);
                    });
            }
            return;
        }
    } catch (std::exception &e) {
        wxMessageBox(e.what(), "parse json failed", wxICON_WARNING);
        Close();
    }
}

void SMUserLogin::RunScript(const wxString &javascript)
{
    // Remember the script we run in any case, so the next time the user opens
    // the "Run Script" dialog box, it is shown there for convenient updating.
    m_javascript = javascript;

    if (!m_browser) return;

    WebView::RunScript(m_browser, javascript);
}
#if wxUSE_WEBVIEW_IE
void SMUserLogin::OnRunScriptObjectWithEmulationLevel(wxCommandEvent &WXUNUSED(evt))
{
    wxWebViewIE::MSWSetModernEmulationLevel();
    RunScript("function f(){var person = new Object();person.name = 'Foo'; \
    person.lastName = 'Bar';return person;}f();");
    wxWebViewIE::MSWSetModernEmulationLevel(false);
}

void SMUserLogin::OnRunScriptDateWithEmulationLevel(wxCommandEvent &WXUNUSED(evt))
{
    wxWebViewIE::MSWSetModernEmulationLevel();
    RunScript("function f(){var d = new Date('10/08/2017 21:30:40'); \
    var tzoffset = d.getTimezoneOffset() * 60000; return \
    new Date(d.getTime() - tzoffset);}f();");
    wxWebViewIE::MSWSetModernEmulationLevel(false);
}

void SMUserLogin::OnRunScriptArrayWithEmulationLevel(wxCommandEvent &WXUNUSED(evt))
{
    wxWebViewIE::MSWSetModernEmulationLevel();
    RunScript("function f(){ return [\"foo\", \"bar\"]; }f();");
    wxWebViewIE::MSWSetModernEmulationLevel(false);
}
#endif

/**
 * Callback invoked when a loading error occurs
 */
void SMUserLogin::OnError(wxWebViewEvent &event)
{
    auto e = "unknown error";
    switch (event.GetInt()) {
    case wxWEBVIEW_NAV_ERR_CONNECTION: e = "wxWEBVIEW_NAV_ERR_CONNECTION"; break;
    case wxWEBVIEW_NAV_ERR_CERTIFICATE: e = "wxWEBVIEW_NAV_ERR_CERTIFICATE"; break;
    case wxWEBVIEW_NAV_ERR_AUTH: e = "wxWEBVIEW_NAV_ERR_AUTH"; break;
    case wxWEBVIEW_NAV_ERR_SECURITY: e = "wxWEBVIEW_NAV_ERR_SECURITY"; break;
    case wxWEBVIEW_NAV_ERR_NOT_FOUND: e = "wxWEBVIEW_NAV_ERR_NOT_FOUND"; break;
    case wxWEBVIEW_NAV_ERR_REQUEST: e = "wxWEBVIEW_NAV_ERR_REQUEST"; break;
    case wxWEBVIEW_NAV_ERR_USER_CANCELLED: e = "wxWEBVIEW_NAV_ERR_USER_CANCELLED"; break;
    case wxWEBVIEW_NAV_ERR_OTHER: e = "wxWEBVIEW_NAV_ERR_OTHER"; break;
    }
    BOOST_LOG_TRIVIAL(fatal) << __FUNCTION__<< boost::format(":SMUserLogin error loading page %1% %2% %3% %4%") % event.GetURL() % event.GetTarget() %e % event.GetString();
    
}

void SMUserLogin::OnScriptResponseMessage(wxCommandEvent &WXUNUSED(evt))
{
    // if (!m_response_js.empty())
    //{
    //    RunScript(m_response_js);
    //}

    // RunScript("This is a message to Web!");
    // RunScript("postMessage(\"AABBCCDD\");");
}

bool  SMUserLogin::ShowErrorPage()
{
    wxString ErrorUrl = from_u8((boost::filesystem::path(resources_dir()) / "web\\login\\error.html").make_preferred().string());
    wxString strlang   = wxGetApp().current_language_code_safe();
    if (strlang != "")
        ErrorUrl = wxString::Format("file://%s/web/login/error.html?lang=%s", from_u8(resources_dir()), strlang);
    load_url(ErrorUrl);

    return true;
}

SMAskUserLoginDialog::SMAskUserLoginDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, _L("Log in"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    const auto background_colour = StateColor::darkModeColorFor(*wxWHITE);
    SetBackgroundColour(background_colour);
    std::string icon_path = (boost::format("%1%/images/Snapmaker_OrcaTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    auto msg_text = new wxStaticText(this, wxID_ANY, _L("Are you sure you want to log in?"));
    msg_text->SetForegroundColour(StateColor::darkModeColorFor(wxColour(0x18, 0x18, 0x1b)));
    msg_text->SetBackgroundColour(background_colour);
    msg_text->SetFont(Label::Body_14);

    auto style_btn = [](Button *b) {
        b->SetPaddingSize(b->FromDIP(wxSize(8, 3)));
        b->SetMinSize(b->FromDIP(wxSize(96, 30)));
        b->SetSize(b->FromDIP(wxSize(96, 30)));
    };

    auto login_btn = new Button(this, _L("Log in"));
    login_btn->SetStyle(ButtonStyle::Confirm, ButtonType::Choice);
    style_btn(login_btn);

    auto cancel_btn = new Button(this, _L("Cancel"));
    cancel_btn->SetStyle(ButtonStyle::Regular, ButtonType::Choice);
    style_btn(cancel_btn);

    wxBoxSizer *btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    btn_sizer->AddStretchSpacer(1);
    btn_sizer->Add(login_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
    btn_sizer->Add(cancel_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 0);

    wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->AddSpacer(FromDIP(16));
    main_sizer->Add(msg_text, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));
    main_sizer->AddSpacer(FromDIP(16));
    main_sizer->Add(btn_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(30));

    login_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(wxID_OK); });
    cancel_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(wxID_CANCEL); });

    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent &e) {
        if (e.GetKeyCode() == WXK_RETURN)
            EndModal(wxID_OK);
        else
            e.Skip();
    });

    SetSizer(main_sizer);
    Layout();
    Fit();
    SetSize(wxSize(FromDIP(580), GetSize().y));
    SetMinSize(GetSize());
    Centre();
    wxGetApp().UpdateDlgDarkUI(this);

    m_keepalive_timer = std::make_unique<wxTimer>(this, wxID_ANY);
    Bind(wxEVT_TIMER, [this](wxTimerEvent &) {
        if (m_keepalive_fn) m_keepalive_fn();
    });
    m_keepalive_timer->Start(30000);
}

SMAskUserLoginDialog::~SMAskUserLoginDialog()
{
    if (m_keepalive_timer)
        m_keepalive_timer->Stop();
}

void SMAskUserLoginDialog::SetKeepAliveCallback(std::function<void()> fn)
{
    m_keepalive_fn = std::move(fn);
}

}} // namespace Slic3r::GUI

