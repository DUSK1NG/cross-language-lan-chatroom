package dev.dusk1ng.lanchat;

import android.Manifest;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;

import java.util.Collections;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;
import java.util.regex.Pattern;

import com.google.firebase.messaging.FirebaseMessagingService;
import com.google.firebase.messaging.RemoteMessage;

public final class MessagePushService extends FirebaseMessagingService {
    static final String PREFERENCES = "lan-chat";
    static final String ENABLED = "message-notifications";
    static volatile boolean foregroundConnected;
    private static final String CHANNEL = "direct-messages";
    private static final Pattern USER_CODE = Pattern.compile("[A-Za-z0-9]{3,16}");

    static boolean hasPermission(Context context) {
        return Build.VERSION.SDK_INT < 33 || context.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                == PackageManager.PERMISSION_GRANTED;
    }

    @Override
    public void onMessageReceived(RemoteMessage message) {
        if (!foregroundConnected && "direct_message".equals(message.getData().get("type"))) {
            String recipient = message.getData().get("recipient");
            String sender = message.getData().get("sender");
            String id = message.getData().get("message_id");
            if (validUserCode(recipient) && validUserCode(sender) && id != null && !id.isEmpty()
                    && getSharedPreferences(PREFERENCES, MODE_PRIVATE).getBoolean(ENABLED, false)) {
                markUnread(this, recipient, sender, id);
            }
            show(this, recipient, sender);
        }
    }

    static boolean validUserCode(String code) {
        return code != null && USER_CODE.matcher(code).matches();
    }

    private static String unreadKey(String recipient, String sender) {
        return "unread:" + recipient.toLowerCase(Locale.ROOT) + ":" + sender.toLowerCase(Locale.ROOT);
    }

    static synchronized void markUnread(Context context, String recipient, String sender, String id) {
        SharedPreferences preferences = context.getSharedPreferences(PREFERENCES, MODE_PRIVATE);
        String key = unreadKey(recipient, sender);
        Set<String> pending = new HashSet<>(preferences.getStringSet(key, Collections.emptySet()));
        if (pending.size() < 100 && pending.add(id)) preferences.edit().putStringSet(key, pending).apply();
    }

    static synchronized void markRead(Context context, String recipient, String sender) {
        context.getSharedPreferences(PREFERENCES, MODE_PRIVATE).edit().remove(unreadKey(recipient, sender)).apply();
    }

    static int unreadCount(Context context, String recipient, String sender) {
        return context.getSharedPreferences(PREFERENCES, MODE_PRIVATE)
                .getStringSet(unreadKey(recipient, sender), Collections.emptySet()).size();
    }

    static void show(Context context, String recipient, String sender) {
        if (!hasPermission(context)
                || !context.getSharedPreferences(PREFERENCES, MODE_PRIVATE).getBoolean(ENABLED, false)) return;

        NotificationManager manager = context.getSystemService(NotificationManager.class);
        manager.createNotificationChannel(new NotificationChannel(CHANNEL, "私信", NotificationManager.IMPORTANCE_HIGH));
        Intent open = new Intent(context, MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        if (validUserCode(recipient) && validUserCode(sender)) {
            open.setData(new Uri.Builder().scheme("lanchat").authority("dm")
                    .appendPath(recipient).appendPath(sender).build());
        }
        PendingIntent action = PendingIntent.getActivity(context, 0, open, PendingIntent.FLAG_IMMUTABLE);
        Notification notification = new Notification.Builder(context, CHANNEL)
                .setSmallIcon(android.R.drawable.ic_dialog_email)
                .setContentTitle("LAN Chat")
                .setContentText("收到新消息")
                .setVisibility(Notification.VISIBILITY_PUBLIC)
                .setCategory(Notification.CATEGORY_MESSAGE)
                .setContentIntent(action)
                .setAutoCancel(true)
                .build();
        manager.notify(open.getDataString(), 1, notification);
    }
}
