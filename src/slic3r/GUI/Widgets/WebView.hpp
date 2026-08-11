#ifndef slic3r_GUI_WebView_hpp_
#define slic3r_GUI_WebView_hpp_

#include <wx/webview.h>

class WebView
{
public:
    static wxWebView *CreateWebView(wxWindow *parent, wxString const &url);
#if wxUSE_WEBVIEW_EDGE
    static bool CheckWebViewRuntime();
    static bool DownloadAndInstallWebViewRuntime();
#endif
    static void LoadUrl(wxWebView * webView, wxString const &url);

    static bool RunScript(wxWebView * webView, wxString const & msg);

    static void RecreateAll();

    // Enable on-disk cookie persistence for the process's webviews so a login
    // session survives restart. Linux (WebKitGTK) only; no-op elsewhere, where
    // the native backend already persists cookies given a user-data folder.
    static void EnablePersistentCookies();
};

#endif // !slic3r_GUI_WebView_hpp_
