package io.a10vm.guest;

import android.content.pm.ApplicationInfo;
import android.os.Parcel;
import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.util.Base64;
import java.util.List;

final class GuestWebView {
    static void reserve() throws Exception {
        Method reserve = Class.forName("android.webkit.WebViewLibraryLoader")
                .getDeclaredMethod("reserveAddressSpaceInZygote");
        reserve.setAccessible(true);
        reserve.invoke(null);
    }

    static void preload(List<String> arguments) throws Exception {
        ClassLoader loader;
        String library;
        if (arguments.get(0).equals("--preload-package") && arguments.size() == 5) {
            Class<?> loaders = Class.forName("android.app.ApplicationLoaders");
            Method getDefault = loaders.getDeclaredMethod("getDefault");
            getDefault.setAccessible(true);
            Method create = loaders.getDeclaredMethod("createAndCacheWebViewClassLoader",
                    String.class, String.class, String.class);
            create.setAccessible(true);
            loader = (ClassLoader) create.invoke(getDefault.invoke(null), arguments.get(1),
                    arguments.get(2), arguments.get(4));
            library = arguments.get(3);
        } else if (arguments.get(0).equals("--preload-app") && arguments.size() == 2) {
            byte[] bytes = Base64.getDecoder().decode(arguments.get(1));
            Parcel parcel = Parcel.obtain();
            ApplicationInfo app;
            try {
                parcel.unmarshall(bytes, 0, bytes.length);
                parcel.setDataPosition(0);
                app = ApplicationInfo.CREATOR.createFromParcel(parcel);
            } finally { parcel.recycle(); }
            Class<?> apk = Class.forName("android.app.LoadedApk");
            Constructor<?> constructor = apk.getDeclaredConstructor(Class.forName("android.app.ActivityThread"),
                    ApplicationInfo.class, Class.forName("android.content.res.CompatibilityInfo"),
                    ClassLoader.class, boolean.class, boolean.class, boolean.class);
            constructor.setAccessible(true);
            loader = (ClassLoader) apk.getMethod("getClassLoader").invoke(
                    constructor.newInstance(null, app, null, null, false, true, false));
            Method getLibrary = Class.forName("android.webkit.WebViewFactory")
                    .getDeclaredMethod("getWebViewLibrary", ApplicationInfo.class);
            getLibrary.setAccessible(true);
            library = (String) getLibrary.invoke(null, app);
        } else throw new IllegalArgumentException("Invalid WebView preload arguments");
        Class.forName("android.webkit.WebViewLibraryLoader").getMethod("loadNativeLibrary",
                ClassLoader.class, String.class).invoke(null, loader, library);
        Method provider = Class.forName("android.webkit.WebViewFactory")
                .getDeclaredMethod("getWebViewProviderClass", ClassLoader.class);
        provider.setAccessible(true);
        Method preload = ((Class<?>) provider.invoke(null, loader)).getMethod("preloadInZygote");
        preload.setAccessible(true);
        if (!Boolean.TRUE.equals(preload.invoke(null)))
            throw new IllegalStateException("WebView provider preload failed");
        System.out.println("webview_provider_preloaded=1");
    }
}
