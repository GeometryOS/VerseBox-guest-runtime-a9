package io.a10vm.guest;

import android.os.Bundle;
import android.os.IBinder;
import android.os.ResultReceiver;
import java.io.FileDescriptor;
import java.io.FileInputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public final class GuestCommand {
    public static void main(String[] args) throws Exception {
        run(args, System.getenv("A10VM_COMMAND_INPUT"));
    }

    static void run(String[] args, String input) throws Exception {
        int result = command(args, input, FileDescriptor.out, FileDescriptor.err);
        System.out.println("guest_command_result=" + result);
        if (result != 0) throw new IllegalStateException("Guest command failed: " + result);
    }

    static String capture(String[] args, String input) throws Exception {
        File output = File.createTempFile("package-command-", ".log", new File("/data/local/tmp"));
        try {
            int result;
            try (FileOutputStream stream = new FileOutputStream(output)) {
                result = command(args, input, stream.getFD(), stream.getFD());
            }
            String text = new String(Files.readAllBytes(output.toPath()), StandardCharsets.UTF_8);
            if (result != 0) throw new IllegalStateException(text.trim());
            return text;
        } finally { output.delete(); }
    }

    private static int command(String[] args, String input, FileDescriptor out, FileDescriptor err) throws Exception {
        if (args.length < 2) throw new IllegalArgumentException("Expected service and command");
        IBinder service = (IBinder) Class.forName("android.os.ServiceManager")
                .getMethod("checkService", String.class).invoke(null, args[0]);
        if (service == null) throw new IllegalStateException("Guest service missing: " + args[0]);
        CountDownLatch finished = new CountDownLatch(1);
        int[] result = {-1};
        ResultReceiver receiver = new ResultReceiver(null) {
            @Override protected void onReceiveResult(int code, Bundle data) {
                result[0] = code;
                finished.countDown();
            }
        };
        try (FileInputStream stream = new FileInputStream(input == null ? "/dev/null" : input)) {
            IBinder.class.getMethod("shellCommand", FileDescriptor.class, FileDescriptor.class,
                    FileDescriptor.class, String[].class, Class.forName("android.os.ShellCallback"),
                    ResultReceiver.class).invoke(service, stream.getFD(), out,
                    err, Arrays.copyOfRange(args, 1, args.length), null, receiver);
            if (!finished.await(120, TimeUnit.SECONDS)) throw new IllegalStateException("Guest command timed out");
        }
        return result[0];
    }
}
