package io.a10vm.guest;

import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.os.Parcel;
import android.os.Parcelable;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.concurrent.TimeUnit;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

public final class ApplicationCodePathsCheck {
    public static void main(String[] args) throws Exception {
        String apk = "/data/app/code-path-check/base.apk";
        File file = new File(apk);
        if (!file.getParentFile().mkdirs() && !file.getParentFile().isDirectory())
            throw new AssertionError("Fixture directory missing");
        try (ZipOutputStream zip = new ZipOutputStream(new FileOutputStream(file))) {
            zip.putNextEntry(new ZipEntry("classes.dex"));
            zip.write(new byte[] {1, 2, 3, 4});
            zip.closeEntry();
        }
        ApplicationInfo original = new ApplicationInfo();
        original.packageName = "code.path.check";
        original.uid = 10001;
        original.sourceDir = original.publicSourceDir = apk;
        original.splitSourceDirs = new String[] {"/data/app/code-path-check/split.apk"};
        original.splitPublicSourceDirs = original.splitSourceDirs.clone();
        original.dataDir = "/data/user/0/code.path.check";
        original.nativeLibraryDir = "/data/app/code-path-check/lib/arm64";
        PackageInfo packageInfo = new PackageInfo();
        packageInfo.applicationInfo = original;
        Field manager = Class.forName("android.app.ActivityThread").getDeclaredField("sPackageManager");
        manager.setAccessible(true);
        Object before = manager.get(null);
        Parcelable.Creator<ApplicationInfo> creator = ApplicationInfo.CREATOR;
        GuestApplication.configureRuntime(29);
        require(manager.get(null) == before, "code_paths_shared_runtime_initialization");
        Class<?> adapter = Class.forName("io.a10vm.framework.PackageCodePaths");
        Method reply = adapter.getMethod("reply", ApplicationInfo.class);
        ApplicationInfo visible = (ApplicationInfo) reply.invoke(null, original);
        ProcessBuilder builder = new ProcessBuilder(System.getenv("A10VM_EXEC_LINKER"), args[0], visible.sourceDir).inheritIO();
        builder.environment().remove("LD_PRELOAD");
        Process reader = builder.start();
        if (!reader.waitFor(3, TimeUnit.SECONDS) || reader.exitValue() != 0) {
            reader.destroyForcibly();
            throw new AssertionError("Direct SVC rejected the delivered APK path");
        }
        String physical = System.getenv("A10VM_ROOTFS") + apk;
        require(physical.equals(visible.sourceDir) && physical.equals(visible.publicSourceDir), "code_paths_base");
        require((System.getenv("A10VM_ROOTFS") + original.splitSourceDirs[0]).equals(visible.splitSourceDirs[0]) &&
                visible.splitSourceDirs[0].equals(visible.splitPublicSourceDirs[0]), "code_paths_splits");
        require(original.dataDir.equals(visible.dataDir) && original.nativeLibraryDir.equals(visible.nativeLibraryDir) &&
                original.uid == visible.uid && original.packageName.equals(visible.packageName), "code_paths_identity");
        require(apk.equals(original.sourceDir) && original.splitSourceDirs != visible.splitSourceDirs, "code_paths_original");
        require(physical.equals(((ApplicationInfo) reply.invoke(null, visible)).sourceDir), "code_paths_idempotent");
        require(creator == ApplicationInfo.CREATOR && apk.equals(copy(original).sourceDir), "code_paths_bind_parcel");
        require(manager.get(null) == before, "code_paths_binder_identity");
        PackageInfo queried = (PackageInfo) adapter.getMethod("reply", PackageInfo.class).invoke(null, packageInfo);
        require(physical.equals(queried.applicationInfo.sourceDir) && apk.equals(original.sourceDir), "code_paths_package_info");
        Class<?> replies = Class.forName("io.a10vm.framework.PackageQueryReplies");
        Method adapt = replies.getMethod("adaptForCaller", int.class, Parcel.class, boolean.class, int.class);
        Class<?> stub = Class.forName("android.content.pm.IPackageManager$Stub");
        Field transaction = stub.getDeclaredField("TRANSACTION_getApplicationInfo");
        transaction.setAccessible(true);
        Parcel output = Parcel.obtain();
        try {
            output.writeNoException();
            output.writeInt(1);
            original.writeToParcel(output, 1);
            require((Boolean) adapt.invoke(null, transaction.getInt(null), output, true, 10001), "code_paths_reply_handled");
            output.setDataPosition(0);
            output.readException();
            require(output.readInt() == 1 && physical.equals(ApplicationInfo.CREATOR.createFromParcel(output).sourceDir),
                    "code_paths_serialized_reply");
            output.setDataSize(0);
            output.setDataPosition(0);
            output.writeInt(-128);
            output.writeInt(8);
            output.writeInt(0);
            output.writeInt(1);
            original.writeToParcel(output, 1);
            adapt.invoke(null, transaction.getInt(null), output, true, 110001);
            output.setDataPosition(0);
            require(output.readInt() == -128 && output.readInt() == 8 && output.readInt() == 0 &&
                    output.readInt() == 1 && physical.equals(ApplicationInfo.CREATOR.createFromParcel(output).sourceDir),
                    "code_paths_strictmode_reply");
            output.setDataSize(0);
            output.setDataPosition(0);
            output.writeNoException();
            output.writeInt(1);
            original.writeToParcel(output, 1);
            adapt.invoke(null, transaction.getInt(null), output, true, 1000);
            output.setDataPosition(0);
            output.readException();
            require(output.readInt() == 1 && apk.equals(ApplicationInfo.CREATOR.createFromParcel(output).sourceDir),
                    "code_paths_system_reply");
            Field versioned = stub.getDeclaredField("TRANSACTION_getPackageInfoVersioned");
            versioned.setAccessible(true);
            output.setDataSize(0);
            output.setDataPosition(0);
            output.writeNoException();
            output.writeInt(1);
            packageInfo.writeToParcel(output, 1);
            adapt.invoke(null, versioned.getInt(null), output, true, 10001);
            output.setDataPosition(0);
            output.readException();
            require(output.readInt() == 1 && physical.equals(
                    PackageInfo.CREATOR.createFromParcel(output).applicationInfo.sourceDir),
                    "code_paths_versioned_reply");
        } finally { output.recycle(); }
        original.sourceDir = original.publicSourceDir = System.getenv("A10VM_HOST_ROOT") + "/host.apk";
        visible = (ApplicationInfo) reply.invoke(null, original);
        require(original.sourceDir.equals(visible.sourceDir), "code_paths_host");
        original.sourceDir = original.publicSourceDir = "/system/product/app/webview/webview.apk";
        visible = (ApplicationInfo) reply.invoke(null, original);
        require(original.sourceDir.equals(visible.sourceDir), "code_paths_system_namespace");
        original.sourceDir = original.publicSourceDir = null;
        original.splitSourceDirs = original.splitPublicSourceDirs = null;
        visible = (ApplicationInfo) reply.invoke(null, original);
        require(visible.sourceDir == null && visible.splitSourceDirs == null && reply.invoke(null, new Object[] {null}) == null,
                "code_paths_nullable");
        PackageQueryClientCheck.run();
        System.out.println("code_paths_complete=1");
    }

    private static ApplicationInfo copy(ApplicationInfo info) {
        Parcel parcel = Parcel.obtain();
        try {
            info.writeToParcel(parcel, 0);
            parcel.setDataPosition(0);
            return ApplicationInfo.CREATOR.createFromParcel(parcel);
        } finally { parcel.recycle(); }
    }

    private static void require(boolean passed, String name) {
        if (!passed) throw new AssertionError(name);
        System.out.println(name + "=1");
    }
}
