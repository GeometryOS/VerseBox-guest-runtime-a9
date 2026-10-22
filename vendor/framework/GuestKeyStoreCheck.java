package io.a10vm.guest;

import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.KeyStore;
import java.security.Signature;
import java.util.Arrays;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

public final class GuestKeyStoreCheck {
    public static void main(String[] args) throws Exception {
        GuestApplication.configureRuntime(29);
        android.os.Looper.prepareMainLooper();
        Class.forName("android.app.ActivityThread").getMethod("systemMain").invoke(null);
        KeyStore keys = KeyStore.getInstance("AndroidKeyStore");
        keys.load(null);
        String alias = args.length > 1 ? args[1] : "a10vm-key-check";
        byte[] plain = "A10VM persisted software key".getBytes(StandardCharsets.US_ASCII);
        if ("create".equals(args[0])) {
            if (keys.containsAlias(alias)) keys.deleteEntry(alias);
            KeyGenerator generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
            generator.init(new KeyGenParameterSpec.Builder(alias,
                    KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                    .setKeySize(256).setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                    .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).build());
            SecretKey key = generator.generateKey();
            if (key.getEncoded() != null) throw new AssertionError("Keystore exported secret key");
            Cipher encrypt = Cipher.getInstance("AES/GCM/NoPadding");
            encrypt.init(Cipher.ENCRYPT_MODE, key);
            byte[] encrypted = encrypt.doFinal(plain), iv = encrypt.getIV();
            byte[] data = new byte[1 + iv.length + encrypted.length];
            data[0] = (byte) iv.length;
            System.arraycopy(iv, 0, data, 1, iv.length);
            System.arraycopy(encrypted, 0, data, 1 + iv.length, encrypted.length);
            Files.write(Paths.get("/data/local/tmp/key-check.bin"), data);
            System.out.println("guest_keystore_aes_created=1");
            KeyPairGenerator rsa = KeyPairGenerator.getInstance(KeyProperties.KEY_ALGORITHM_RSA, "AndroidKeyStore");
            rsa.initialize(new KeyGenParameterSpec.Builder(alias + "-rsa",
                    KeyProperties.PURPOSE_SIGN | KeyProperties.PURPOSE_VERIFY).setKeySize(2048)
                    .setDigests(KeyProperties.DIGEST_SHA256)
                    .setSignaturePaddings(KeyProperties.SIGNATURE_PADDING_RSA_PKCS1).build());
            KeyPair pair = rsa.generateKeyPair();
            Signature signer = Signature.getInstance("SHA256withRSA");
            signer.initSign(pair.getPrivate()); signer.update(plain);
            byte[] signature = signer.sign();
            signer.initVerify(pair.getPublic()); signer.update(plain);
            if (!signer.verify(signature)) throw new AssertionError("Keystore RSA signature changed");
            System.out.println("guest_keystore_rsa_verified=1");
        }
        if ("cleared".equals(args[0])) {
            if (keys.containsAlias(alias) || keys.containsAlias(alias + "-rsa"))
                throw new AssertionError("UID clear retained aliases");
            System.out.println("guest_keystore_uid_cleared=1");
        } else {
            byte[] data = Files.readAllBytes(Paths.get("/data/local/tmp/key-check.bin"));
            int length = data[0] & 255;
            Cipher decrypt = Cipher.getInstance("AES/GCM/NoPadding");
            decrypt.init(Cipher.DECRYPT_MODE, (SecretKey) keys.getKey(alias, null),
                    new GCMParameterSpec(128, Arrays.copyOfRange(data, 1, 1 + length)));
            if (!Arrays.equals(plain, decrypt.doFinal(data, 1 + length, data.length - length - 1)))
                throw new AssertionError("Persisted AES-GCM decrypt changed data");
            System.out.println("guest_keystore_aes_verified=1");
        }
        System.out.println("guest_keystore_complete=1");
    }
}
