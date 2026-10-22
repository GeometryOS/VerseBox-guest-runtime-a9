package io.a10vm.guest;

import java.util.Arrays;
import java.util.Collections;
import java.util.List;

final class AppRuntimeOptions {
    static List<String> forProcess(String name) {
        // Android 10 JIT inlining misdispatches coroutine interface calls in this app.
        // Keep compilation enabled and scope the workaround to its process family.
        if ("com.ss.android.ugc.aweme".equals(name) || name.startsWith("com.ss.android.ugc.aweme:"))
            return Arrays.asList("-Xcompiler-option", "--inline-max-code-units=0");
        return Collections.emptyList();
    }
}
