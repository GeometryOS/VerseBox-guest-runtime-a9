package io.a10vm.guest;

import java.lang.reflect.Method;
import dalvik.system.PathClassLoader;

/** Checks coroutine keys and context folding using the installed APK's classes. */
public final class CoroutineContextCheck {
    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("Expected APK path");
        ClassLoader loader = new PathClassLoader(args[0], CoroutineContextCheck.class.getClassLoader());
        Class<?> context = loader.loadClass("kotlin.coroutines.CoroutineContext");
        Class<?> key = loader.loadClass("kotlin.coroutines.CoroutineContext$Key");
        Class<?> jobType = loader.loadClass("kotlinx.coroutines.Job");
        Object jobKey = jobType.getField("Key").get(null);
        Object job = loader.loadClass("kotlinx.coroutines.JobKt").getMethod("Job", jobType)
                .invoke(null, new Object[] {null});
        Method get = context.getMethod("get", key), plus = context.getMethod("plus", context);
        Object clockKey = loader.loadClass("X.18Kx").getField("u8").get(null);
        Object clock = loader.loadClass("X.18LB").getConstructor(android.view.Choreographer.class,
                loader.loadClass("X.18LD")).newInstance(null, null);
        if (jobKey == null || clockKey == null) throw new AssertionError("Missing coroutine keys");
        for (int phase = 0; phase < 2; phase++) {
            for (int i = 0; i < 20000; i++) {
                Object combined = plus.invoke(job, clock);
                if (get.invoke(combined, jobKey) != job || get.invoke(combined, clockKey) != clock)
                    throw new AssertionError("Context key lookup failed at phase=" + phase + " iteration=" + i);
            }
            if (phase == 0) Thread.sleep(1000);
        }
        System.out.println("coroutine_context_complete=1");
    }

}
