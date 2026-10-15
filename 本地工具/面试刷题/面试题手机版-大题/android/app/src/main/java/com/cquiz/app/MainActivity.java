package com.cquiz.app;

import android.app.Activity;
import android.os.Bundle;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

/**
 * 刷题软件 · Android 版
 * 用 WebView 加载本地单文件应用（assets/index.html，题库已内嵌）。
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
        // 应用内返回键交给 WebView 处理（本应用基本无历史，直接退出）
        super.onBackPressed();
    }

    @Override
    protected void onDestroy() {
        if (webView != null) {
            webView.destroy();
        }
        super.onDestroy();
    }
}
