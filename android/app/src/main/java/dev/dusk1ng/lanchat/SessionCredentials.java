package dev.dusk1ng.lanchat;

import android.content.Context;
import android.content.SharedPreferences;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.security.KeyStore;

import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

final class SessionCredentials {
    private static final String ALIAS = "lan-chat-sign-in";
    record Credentials(String username, String password) {}

    private static SharedPreferences storage(Context context) {
        return context.getSharedPreferences("sign-in", Context.MODE_PRIVATE);
    }

    static Credentials load(Context context) throws GeneralSecurityException, IOException, JSONException {
        String encoded = storage(context).getString("encrypted", null);
        if (encoded == null) return null;
        JSONObject envelope = new JSONObject(encoded);
        KeyStore keys = KeyStore.getInstance("AndroidKeyStore");
        keys.load(null);
        if (!keys.containsAlias(ALIAS)) throw new GeneralSecurityException("登录加密密钥不存在");
        try {
            byte[] iv = Base64.decode(envelope.getString("iv"), Base64.NO_WRAP);
            if (iv.length != 12) throw new IOException("登录凭据 IV 长度无效：" + iv.length);
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.DECRYPT_MODE, keys.getKey(ALIAS, null), new GCMParameterSpec(128, iv));
            JSONObject value = new JSONObject(new String(cipher.doFinal(Base64.decode(envelope.getString("data"), Base64.NO_WRAP)), StandardCharsets.UTF_8));
            String username = value.getString("username");
            String password = value.getString("password");
            if (username.isEmpty() || password.isEmpty()) throw new IOException("已保存的账号或密码为空");
            return new Credentials(username, password);
        } catch (IllegalArgumentException error) {
            throw new IOException("登录凭据编码无效", error);
        }
    }

    static void save(Context context, String username, String password) throws GeneralSecurityException, IOException, JSONException {
        KeyStore keys = KeyStore.getInstance("AndroidKeyStore");
        keys.load(null);
        SecretKey key;
        if (keys.containsAlias(ALIAS)) {
            key = (SecretKey) keys.getKey(ALIAS, null);
        } else {
            KeyGenerator generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
            generator.init(new KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                    .setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).build());
            key = generator.generateKey();
        }
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
        cipher.init(Cipher.ENCRYPT_MODE, key);
        byte[] clear = new JSONObject().put("username", username).put("password", password).toString().getBytes(StandardCharsets.UTF_8);
        String encoded = new JSONObject().put("iv", Base64.encodeToString(cipher.getIV(), Base64.NO_WRAP))
                .put("data", Base64.encodeToString(cipher.doFinal(clear), Base64.NO_WRAP)).toString();
        if (!storage(context).edit().putString("encrypted", encoded).commit()) throw new IOException("登录凭据写入失败");
    }

    static void clear(Context context) {
        if (!storage(context).edit().clear().commit()) throw new IllegalStateException("登录凭据删除失败");
    }
}
