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

import java.util.Map;
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
    static final String ACCOUNT = "notification-account";
    static volatile boolean foregroundConnected;
    private static final Pattern USER_CODE = Pattern.compile("[A-Za-z0-9]{3,16}");
    private static final Pattern GROUP_ID = Pattern.compile("[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}");

    static boolean hasPermission(Context context) {
        return Build.VERSION.SDK_INT < 33 || context.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                == PackageManager.PERMISSION_GRANTED;
    }

    @Override
    public void onMessageReceived(RemoteMessage message) {
        if (foregroundConnected || !getSharedPreferences(PREFERENCES, MODE_PRIVATE).getBoolean(ENABLED, false)) return;
        Map<String, String> data = message.getData();
        String recipient = data.get("recipient");
        if (!validUserCode(recipient) || !recipient.equalsIgnoreCase(
                getSharedPreferences(PREFERENCES, MODE_PRIVATE).getString(ACCOUNT, null))) return;
        if ("direct_message".equals(data.get("type"))) {
            String sender = data.get("sender");
            String id = data.get("message_id");
            if (!validUserCode(sender) || id == null || id.isEmpty()) return;
            markUnread(this, recipient, sender, id);
            show(this, recipient, sender, false);
        } else if ("group_message".equals(data.get("type"))) {
            String group = data.get("groupId");
            String sender = data.get("senderUserCode");
            String id = data.get("messageId");
            if (!validGroupId(group) || !validUserCode(sender) || id == null || id.isEmpty()) return;
            markUnread(this, recipient, group, sender.toLowerCase(Locale.ROOT) + ":" + id);
            show(this, recipient, group, true);
        }
    }

    static boolean validUserCode(String code) {
        return code != null && USER_CODE.matcher(code).matches();
    }

    static boolean validGroupId(String id) {
        return id != null && GROUP_ID.matcher(id).matches();
    }

    private static Uri route(String recipient, String conversation, boolean group) {
        return new Uri.Builder().scheme("lanchat").authority(group ? "group" : "dm")
                .appendPath(recipient.toLowerCase(Locale.ROOT))
                .appendPath(conversation.toLowerCase(Locale.ROOT)).build();
    }

    static void clearGroup(Context context, String recipient, String group) {
        markRead(context, recipient, group);
        context.getSystemService(NotificationManager.class).cancel(route(recipient, group, true).toString(), 1);
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

    static void show(Context context, String recipient, String conversation, boolean group) {
        if (!hasPermission(context)
                || !context.getSharedPreferences(PREFERENCES, MODE_PRIVATE).getBoolean(ENABLED, false)) return;

        NotificationManager manager = context.getSystemService(NotificationManager.class);
        String channel = group ? "group-messages" : "direct-messages";
        manager.createNotificationChannel(new NotificationChannel(channel, group ? "群聊" : "私信",
                NotificationManager.IMPORTANCE_HIGH));
        Intent open = new Intent(context, MainActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP)
                .setData(route(recipient, conversation, group));
        PendingIntent action = PendingIntent.getActivity(context, 0, open, PendingIntent.FLAG_IMMUTABLE);
        Notification notification = new Notification.Builder(context, channel)
                .setSmallIcon(android.R.drawable.ic_dialog_email)
                .setContentTitle("LAN Chat")
                .setContentText(group ? "收到新群消息" : "收到新消息")
                .setVisibility(Notification.VISIBILITY_PUBLIC)
                .setCategory(Notification.CATEGORY_MESSAGE)
                .setContentIntent(action)
                .setAutoCancel(true)
                .build();
        manager.notify(open.getDataString(), 1, notification);
    }
}
