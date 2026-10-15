package com.stmlookup.app;

import android.app.Activity;
import android.os.Bundle;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

/**
 * STM32 标准库速查 · Android 版
 * 用 WebView 加载本地单文件应用（assets/index.html，数据已内嵌）。
 */
public class MainActivity extends Activity {

    private WebView webView;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        webView = new WebView(this);
        WebSettings s = webView.getSettings();
        s.setJavaScriptEnabled(true);          // 应用需要 JS
        s.setDomStorageEnabled(true);          // localStorage 进度/笔记
        s.setAllowFileAccess(true);
        s.setLoadWithOverviewMode(true);
        s.setUseWideViewPort(true);
        webView.setWebViewClient(new WebViewClient());
        setContentView(webView);
        webView.loadUrl("file:///android_asset/index.html");
    }

    @Override
    public void onBackPressed() {
        // 详情页时：返回键先回到列表页；已在列表页才退出应用
        if (webView != null) {
            webView.evaluateJavascript(
                "(function(){ if (document.body.classList.contains('show-detail') && window.goBackToList) { window.goBackToList(); return 'in'; } return 'out'; })()",
                value -> {
                    if ("\"out\"".equals(value)) {
                        finish();
                    }
                });
        } else {
            super.onBackPressed();
        }
    }

    @Override
    protected void onDestroy() {
        if (webView != null) {
            webView.destroy();
        }
        super.onDestroy();
    }
}
