package dev.dusk1ng.lanchat;

import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.animation.ArgbEvaluator;
import android.animation.ValueAnimator;
import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.Context;
import android.content.res.ColorStateList;
import android.content.res.Configuration;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.ImageDecoder;
import android.graphics.Insets;
import android.graphics.Matrix;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.LayerDrawable;
import android.graphics.drawable.InsetDrawable;
import android.graphics.drawable.RippleDrawable;
import android.graphics.drawable.StateListDrawable;
import android.graphics.fonts.Font;
import android.graphics.fonts.FontFamily;
import android.graphics.fonts.FontStyle;
import android.net.Uri;
import android.net.ConnectivityManager;
import android.net.Network;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.text.Editable;
import android.text.InputType;
import android.text.Layout;
import android.text.Spannable;
import android.text.SpannableStringBuilder;
import android.text.TextUtils;
import android.text.TextWatcher;
import android.text.style.ForegroundColorSpan;
import android.text.style.StyleSpan;
import android.transition.ChangeBounds;
import android.transition.TransitionManager;
import android.util.Base64;
import android.util.LruCache;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.View;
import android.view.ViewTreeObserver;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.animation.DecelerateInterpolator;
import android.view.animation.OvershootInterpolator;
import android.view.animation.PathInterpolator;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.PopupWindow;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;

import androidx.core.content.FileProvider;

import com.google.firebase.FirebaseApp;
import com.google.firebase.messaging.FirebaseMessaging;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.time.Instant;
import java.time.LocalDate;
import java.time.ZoneId;
import java.time.format.DateTimeFormatter;
import java.time.format.DateTimeParseException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.regex.Pattern;

public final class MainActivity extends Activity {
    private static final int IMAGE_PICK = 1;
    private static final int NOTIFICATION_PERMISSION = 2;
    private static final int FILE_PICK = 3;
    private static final int SAVE_DOCUMENT = 4;
    private static final int MAX_IMAGE = 4 * 1024 * 1024;
    private static final int HISTORY_PAGE = 30;
    private static final int INITIAL_HISTORY = 16;
    private static final String RECALLED_TEXT = "消息已撤回";
    // Claude 中性色卡（与桌面 tokens.css 同源）：浅色纸白底 / 深色炭黑底，强调色 terracotta。
    // 全部视图在 onCreate 里构建，所以 applyPalette() 必须在建视图之前按 uiMode 定好这些值。
    private static boolean DARK;
    private static int INK;
    private static int MUTED;
    /** 文本/图标用的强调色：深色下要提亮才能在 #191919 上达到 4.5:1。 */
    private static int ACCENT;
    /** 填充用强调色（主按钮、自己的气泡、未读角标）：白字对比 4.87:1，深浅两套共用。 */
    private static int ACCENT_FILL;
    private static int TINT;
    private static int PAPER;
    private static int WHITE;
    private static int OUTLINE;
    private static int DANGER;
    private static int SOFT_CARD;
    private static int GRAIN_PEER;
    private static int DISABLED_INK;
    private static int RIPPLE_ACCENT;
    private static int RIPPLE_PRIMARY;
    /** 自己的气泡：浅色依旧 terracotta（品牌色块），深色改 Claude 式中性灰块 + 亮字。 */
    private static int BUBBLE_SELF;
    private static int BUBBLE_SELF_INK;

    private static void applyPalette(boolean dark) {
        DARK = dark;
        if (dark) {
            INK = 0xfff5f4ef;
            MUTED = 0xffa3a099;
            ACCENT = 0xffe08a6d;
            ACCENT_FILL = 0xffb4552f;
            TINT = 0xff242422;
            PAPER = 0xff191919;
            WHITE = 0xff1f1f1f;
            OUTLINE = 0xff3a3936;
            DANGER = 0xffe0655a;
            SOFT_CARD = 0xff232322;
            GRAIN_PEER = 0xff3b2b23;
            DISABLED_INK = 0xff6f6b64;
            RIPPLE_ACCENT = 0x33e08a6d;
            RIPPLE_PRIMARY = 0x40ffffff;
            BUBBLE_SELF = 0xff2b2b2a;
            BUBBLE_SELF_INK = 0xfff5f4ef;
        } else {
            INK = 0xff161616;
            MUTED = 0xff6b6b66;
            ACCENT = 0xffb4562f;
            ACCENT_FILL = 0xffb4562f;
            TINT = 0xfff2f0ec;
            PAPER = 0xffffffff;
            WHITE = 0xfffafafa;
            OUTLINE = 0xffe6e4dd;
            DANGER = 0xffc4453a;
            SOFT_CARD = 0xfff7f6f3;
            GRAIN_PEER = 0xffefe6df;
            DISABLED_INK = 0xff3d3d3a;
            RIPPLE_ACCENT = 0x20b4562f;
            RIPPLE_PRIMARY = 0x33ffffff;
            BUBBLE_SELF = 0xffb4562f;
            BUBBLE_SELF_INK = 0xffffffff;
        }
    }
    private static final DateTimeFormatter CLOCK = DateTimeFormatter.ofPattern("HH:mm");
    private static final DateTimeFormatter DATE = DateTimeFormatter.ofPattern("yyyy年M月d日");
    private static final Pattern BOT_BOLD = Pattern.compile("\\*\\*([^*\\n]+)\\*\\*");
    private static final Pattern BOT_BULLET = Pattern.compile("(?m)^[ \\t]*[-*][ \\t]+");
    private static final Pattern BOT_EMPTY_LINES = Pattern.compile("\\n{3,}");
    private static final String[] EMOJI = {"😀", "😂", "🥹", "😊", "😉", "😍", "🤔", "😅", "😢", "😴", "😎", "🙃",
            "👍", "👏", "🙌", "🙏", "❤️", "🎉", "✨", "🔥", "👀", "💬", "🤝", "✅"};
    private static final DecelerateInterpolator ARRIVE = new DecelerateInterpolator(1.6f);
    // 动效令牌，与桌面 frontend/src/styles/tokens.css 的 Claude motion 对齐：
    // feedback 120 / surface 150 / open 250 / enter 320 / flight 400。
    private static final long MOTION_FEEDBACK = 120L;
    private static final long MOTION_SURFACE = 150L;
    private static final long MOTION_OPEN = 250L;
    private static final long MOTION_ENTER = 320L;
    private static final long MOTION_FLIGHT = 400L;

    private final ExecutorService writers = Executors.newSingleThreadExecutor();
    private final ExecutorService fileTransfers = Executors.newSingleThreadExecutor();
    private final ExecutorService imageLoads = Executors.newSingleThreadExecutor();
    private final ExecutorService socialRequests = Executors.newSingleThreadExecutor();
    private final Handler main = new Handler(Looper.getMainLooper());
    private final Map<String, Contact> contacts = new LinkedHashMap<>();
    private final Map<String, Group> groupDirectory = new LinkedHashMap<>();
    private final Map<String, Map<String, ChatMessage>> histories = new LinkedHashMap<>();
    private final Map<String, String> historyCursor = new LinkedHashMap<>();
    private final Set<String> initializedHistory = new HashSet<>();
    private final Map<String, String> pendingHistory = new LinkedHashMap<>();
    private final Map<String, TextView> visibleMessageTimes = new LinkedHashMap<>();
    private final Map<String, PendingText> pendingTexts = new LinkedHashMap<>();
    private final Map<File, ImageTile> visibleImages = new LinkedHashMap<>();
    private final Set<File> queuedThumbnails = new HashSet<>();
    private final Set<File> missingImages = new LinkedHashSet<>();
    private final LruCache<File, Bitmap> thumbnails = new LruCache<>(12 * 1024 * 1024) {
        @Override protected int sizeOf(File file, Bitmap bitmap) { return bitmap.getByteCount(); }
    };
    private final Runnable refreshContacts = new Runnable() {
        @Override public void run() {
            if (connection != null) {
                send("dm_contacts");
                main.postDelayed(this, 120000);
            }
        }
    };

    private FrameLayout screen;
    private LinearLayout page;
    private OnBackInvokedCallback backCallback;
    private LinearLayout contactRows;
    private LinearLayout friendRows;
    private LinearLayout groupRows;
    private Runnable socialBack;
    private boolean socialAvailable;
    private boolean groupsAvailable;
    private boolean groupManagementAvailable;
    private boolean groupAttachmentsAvailable;
    private boolean botSettingsAvailable;
    private boolean botKeySyncAvailable;
    private Contact groupDetails;
    private boolean directorySelected;
    private LinearLayout messages;
    private ConversationScroll scroll;
    private EditText draftInput;
    private TextView connectionBanner;
    private String connectionStatus = "";
    private ChatSearch chatSearch;
    private FrameLayout chatBody;
    private ComposerPanels composerPanels;
    private ChatMessage selectedQuote;
    private LinearLayout quoteBar;
    private TextView quotePreview;
    private int visibleHistoryLimit = INITIAL_HISTORY;
    private String revealedSearchMessage;
    private LocalDate renderedDate;
    private Typeface regularFont;
    private Typeface mediumFont;
    private Typeface boldFont;
    private ChatConnection connection;
    private Contact active;
    private String ownCode;
    private String username;
    private String pendingImagePeer;
    private boolean pendingImageGroup;
    private Uri pendingImageUri;
    private String pendingFilePeer;
    private boolean pendingFileGroup;
    private Uri pendingFileUri;
    private SavedDocument pendingSave;
    private LinearLayout transferChip;
    private ProgressBar transferBar;
    private TextView transferChipText;
    private String transferTitle = "";
    private boolean transferringFile;
    private ImageDownload download;
    private boolean imagesAvailable;
    private boolean filesAvailable;
    private boolean editingAvailable;
    private boolean authenticating;
    private boolean registering;
    private boolean settings;
    private SessionCredentials.Credentials savedAccount;
    private boolean foreground;
    private int generation;
    private int reconnectDelay = 2000;
    private final Runnable reconnect = () -> {
        if (foreground && savedAccount != null && connection == null && !authenticating) {
            login(savedAccount.username(), savedAccount.password(), null);
        }
    };
    private final ConnectivityManager.NetworkCallback networkCallback = new ConnectivityManager.NetworkCallback() {
        @Override public void onAvailable(Network network) {
            main.post(() -> { main.removeCallbacks(reconnect); reconnect.run(); });
        }
        @Override public void onLost(Network network) {
            main.post(() -> {
                if (!foreground || savedAccount == null) return;
                disconnect();
                showConnectionStatus("网络已断开，正在等待恢复…");
                main.postDelayed(reconnect, reconnectDelay);
            });
        }
    };

    private record Contact(String code, String name, boolean group) {
        Contact(String code, String name) { this(code, name, false); }
    }
    private record Group(Contact contact, int memberCount, String role, String announcement,
                         String invitedBy, long joinedAfter) {}
    private record PendingText(Contact peer, String text, Instant time) {}
    private record ChatMessage(String id, String author, String authorName, String text, int imageSize, Instant time, String delivery,
                               long revision, boolean recalled, ChatConnection.Attachment attachment) {
        ChatMessage withDelivery(String state) {
            return new ChatMessage(id, author, authorName, text, imageSize, time, state, revision, recalled, attachment);
        }

        ChatMessage withRecalled() {
            return new ChatMessage(id, author, authorName, RECALLED_TEXT, 0, time, delivery, revision, true, null);
        }
    }
    private static final Comparator<ChatMessage> MESSAGE_ORDER = Comparator
            .comparing(ChatMessage::time, Comparator.nullsFirst(Comparator.naturalOrder()))
            .thenComparing(MainActivity::messageKey);
    private record ImageDownload(Contact peer, ChatMessage message, String command, ByteArrayOutputStream bytes) {}
    private record ImageTile(Contact peer, ChatMessage message, ImageView thumbnail, TextView status) {}
    private record SavedDocument(File source, String name, String mime, boolean temporary) {}

    private final class MessageArrival extends LinearLayout {
        private final Paint grain = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final float density;
        private final boolean outgoing;
        private final ValueAnimator gather = ValueAnimator.ofFloat(0f, 1f);
        private float progress = 1f;

        MessageArrival(Context context, boolean outgoing) {
            super(context);
            density = getResources().getDisplayMetrics().density;
            this.outgoing = outgoing;
            gather.setDuration(900);
            gather.setInterpolator(ARRIVE);
            gather.addUpdateListener(animator -> {
                progress = (float) animator.getAnimatedValue();
                invalidate();
            });
        }

        void play(View bubbleGroup) {
            boolean fadeIn = fadeInNewText();
            bubbleGroup.setScaleX(1f);
            bubbleGroup.setScaleY(1f);
            bubbleGroup.setTranslationX(0f);
            bubbleGroup.setTranslationY(0f);
            if (growBubble()) {
                // 原地长出：0.94 → 1，同时上浮 4dp，与桌面 cirrus-grow 同参数。
                bubbleGroup.setAlpha(fadeIn ? 0.4f : 1f);
                bubbleGroup.setScaleX(0.94f);
                bubbleGroup.setScaleY(0.94f);
                bubbleGroup.setTranslationY(4f * density);
                bubbleGroup.animate().alpha(1f).scaleX(1f).scaleY(1f).translationY(0f)
                        .setDuration(MOTION_ENTER).setInterpolator(ARRIVE).start();
            } else {
                // 从输入框飞上来：下方 28dp 起、淡入，与桌面 --dur-flight 同参数。
                bubbleGroup.setAlpha(fadeIn ? 0.4f : 1f);
                bubbleGroup.setTranslationY(28f * density);
                bubbleGroup.animate().alpha(1f).translationY(0f)
                        .setDuration(MOTION_FLIGHT).setInterpolator(ARRIVE).start();
            }
            gather.start();
        }

        @Override protected void dispatchDraw(Canvas canvas) {
            super.dispatchDraw(canvas);
            if (progress >= 1f) return;
            LinearLayout group = (LinearLayout) getChildAt(0);
            View bubble = group.getChildAt(0);
            if (bubble.getWidth() == 0) return;
            float left = group.getX() + bubble.getX();
            float top = group.getY() + bubble.getY();
            float width = bubble.getWidth();
            float lineHeight = Math.min(bubble.getHeight(), 48f * density);
            float remaining = 1f - progress;
            for (int i = 0; i < 32; i++) {
                float targetX = left + width * ((i * 17 % 31) / 30f);
                float targetY = top + lineHeight * ((i * 11 % 29) / 28f);
                double angle = i * 2.39996323;
                float distance = (14f + i % 9 * 2f) * density * remaining;
                float x = targetX + (float) Math.cos(angle) * distance;
                float y = targetY + (float) Math.sin(angle) * distance;
                grain.setColor(i % 3 == 0 ? ACCENT : outgoing ? GRAIN_PEER : ACCENT_FILL);
                grain.setAlpha(Math.round(180f * remaining * (1f - remaining * 0.5f)));
                canvas.drawCircle(x, y, density * (1f + i % 3 * 0.3f), grain);
            }
        }

        @Override protected void onDetachedFromWindow() {
            gather.cancel();
            super.onDetachedFromWindow();
        }
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        applyPalette((getResources().getConfiguration().uiMode & android.content.res.Configuration.UI_MODE_NIGHT_MASK)
                == android.content.res.Configuration.UI_MODE_NIGHT_YES);
        getWindow().setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(PAPER));
        getWindow().setStatusBarColor(PAPER);
        getWindow().setNavigationBarColor(PAPER);
        if (state != null) {
            pendingImagePeer = state.getString("imagePeer");
            pendingImageGroup = state.getBoolean("imageGroup");
            pendingImageUri = state.getParcelable("imageUri");
            pendingFilePeer = state.getString("filePeer");
            pendingFileGroup = state.getBoolean("fileGroup");
            pendingFileUri = state.getParcelable("fileUri");
            if (state.containsKey("saveSource")) {
                pendingSave = new SavedDocument(new File(getCacheDir(), state.getString("saveSource")),
                        state.getString("saveName"), state.getString("saveMime"), state.getBoolean("saveTemporary"));
            }
        }
        regularFont = chatTypeface(400);
        mediumFont = chatTypeface(500);
        boldFont = chatTypeface(650);
        try {
            savedAccount = SessionCredentials.load(this);
            if (savedAccount != null) username = savedAccount.username();
            showLogin(savedAccount == null ? "" : "正在恢复登录…");
        } catch (java.security.GeneralSecurityException | IOException | JSONException error) {
            showLogin("无法读取已保存登录，请重新登录 · " + error.getMessage());
        }
    }

    @Override protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        state.putString("imagePeer", pendingImagePeer);
        state.putBoolean("imageGroup", pendingImageGroup);
        state.putParcelable("imageUri", pendingImageUri);
        state.putString("filePeer", pendingFilePeer);
        state.putBoolean("fileGroup", pendingFileGroup);
        state.putParcelable("fileUri", pendingFileUri);
        if (pendingSave != null) {
            state.putString("saveSource", pendingSave.source().getName());
            state.putString("saveName", pendingSave.name());
            state.putString("saveMime", pendingSave.mime());
            state.putBoolean("saveTemporary", pendingSave.temporary());
        }
    }

    @Override protected void onStart() {
        super.onStart();
        foreground = true;
        getSystemService(ConnectivityManager.class).registerDefaultNetworkCallback(networkCallback);
        reconnect.run();
    }

    @Override protected void onStop() {
        foreground = false;
        getSystemService(ConnectivityManager.class).unregisterNetworkCallback(networkCallback);
        disconnect();
        super.onStop();
    }

    private void showLogin(String error) {
        page = newPage();
        ScrollView accountScroll = new ScrollView(this);
        accountScroll.setFillViewport(true);
        accountScroll.setVerticalScrollBarEnabled(false);
        page.addView(accountScroll, new LinearLayout.LayoutParams(-1, 0, 1));
        LinearLayout accountPage = column();
        accountPage.setGravity(Gravity.CENTER);
        accountPage.setPadding(0, dp(8), 0, dp(16));
        accountScroll.addView(accountPage, new ScrollView.LayoutParams(-1, -2));

        LinearLayout content = column();
        int width = Math.min(dp(468), getResources().getDisplayMetrics().widthPixels - dp(52));
        accountPage.addView(content, new LinearLayout.LayoutParams(width, -2));
        FrameLayout cover = new FrameLayout(this);
        cover.setBackground(bookCard(24));
        TextView brand = title("LAN Chat");
        brand.setGravity(Gravity.BOTTOM | Gravity.START);
        brand.setPadding(dp(20), 0, dp(20), dp(16));
        cover.addView(brand, new FrameLayout.LayoutParams(-1, -1));
        content.addView(cover, new LinearLayout.LayoutParams(-1, dp(120)));

        LinearLayout form = column();
        form.setPadding(dp(18), dp(16), dp(18), dp(18));
        form.setBackground(card(WHITE, 23));
        addSpaced(content, form, 14);
        TextView heading = title(registering ? "创建账号" : "登录");
        heading.setTextSize(24);
        form.addView(heading);

        EditText user = field("用户名", InputType.TYPE_CLASS_TEXT);
        user.setText(username);
        user.setSingleLine(true);
        user.setAutofillHints(View.AUTOFILL_HINT_USERNAME);
        addSpaced(form, user, 14);
        EditText code = field("用户代码", InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        code.setSingleLine(true);
        if (registering) addSpaced(form, code, 8);
        EditText password = field("密码", InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        password.setAutofillHints(registering ? "newPassword" : View.AUTOFILL_HINT_PASSWORD);
        addSpaced(form, password, 8);
        if (!error.isEmpty()) {
            TextView status = label(error, 13, authenticating ? MUTED : DANGER);
            status.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
            addSpaced(form, status, 8);
        }
        Button proceed = button(authenticating ? "连接中…" : registering ? "注册并登录" : "登录", true, () -> {
            String name = user.getText().toString().trim();
            String secret = password.getText().toString();
            String registrationCode = registering ? code.getText().toString().trim() : null;
            if (name.isEmpty() || secret.isEmpty() || (registering && registrationCode.isEmpty())) {
                toast("请填写账号信息");
                return;
            }
            if (savedAccount != null && !savedAccount.username().equalsIgnoreCase(name)) {
                clearSession();
                forgetAccount();
            }
            username = name;
            login(name, secret, registrationCode);
        });
        proceed.setEnabled(!authenticating);
        addSpaced(form, proceed, 12);
        Button changeMode = button(registering ? "返回登录" : "注册账号", false, () -> {
            registering = !registering;
            username = user.getText().toString().trim();
            showLogin("");
        });
        changeMode.setEnabled(!authenticating);
        addSpaced(form, changeMode, 8);
        if (!authenticating && error.isEmpty()) enter(content, 0, 14, 275);
    }

    private void login(String name, String password, String code) {
        main.removeCallbacks(reconnect);
        authenticating = true;
        int attempt = ++generation;
        showConnectionStatus(savedAccount == null ? "正在连接…" : "正在恢复连接…");
        new Thread(() -> {
            try (ChatConnection session = ChatConnection.open()) {
                JSONObject identity = session.login(name, password, code);
                String userCode = identity.getString("user_code");
                String accountName = identity.getString("username");
                JSONArray capabilities = identity.getJSONArray("capabilities");
                boolean supportsImages = supports(capabilities, "online-images-v1");
                boolean supportsFiles = supports(capabilities, "online-files-v1");
                boolean supportsEditing = supports(capabilities, "online-message-edit-v1");
                boolean supportsSocial = supports(capabilities, "online-social-v1");
                boolean supportsGroups = supports(capabilities, "online-groups-v1");
                boolean supportsGroupManagement = supports(capabilities, "online-group-management-v1");
                boolean supportsGroupAttachments = supports(capabilities, "online-group-attachments-v1");
                boolean supportsBotSettings = supports(capabilities, "online-bot-settings-v1");
                boolean supportsBotKeySync = supports(capabilities, "online-bot-key-sync-v1");
                main.post(() -> {
                    if (isDestroyed() || generation != attempt) {
                        closeConnection(session);
                        return;
                    }
                    boolean firstLogin = ownCode == null;
                    try {
                        if (firstLogin && (savedAccount == null || !savedAccount.username().equals(accountName)
                                || !savedAccount.password().equals(password))) {
                            SessionCredentials.save(this, accountName, password);
                        }
                    } catch (java.security.GeneralSecurityException | IOException | JSONException error) {
                        ++generation;
                        authenticating = false;
                        closeConnection(session);
                        showLogin("无法保存登录 · " + error.getMessage());
                        return;
                    }
                    savedAccount = new SessionCredentials.Credentials(accountName, password);
                    connection = session;
                    MessagePushService.foregroundConnected = foreground;
                    ownCode = userCode;
                    preferences().edit().putString(MessagePushService.ACCOUNT, key(userCode)).apply();
                    username = accountName;
                    imagesAvailable = supportsImages;
                    filesAvailable = supportsFiles;
                    editingAvailable = supportsEditing;
                    socialAvailable = supportsSocial;
                    groupsAvailable = supportsGroups;
                    groupManagementAvailable = supportsGroupManagement;
                    groupAttachmentsAvailable = supportsGroupAttachments;
                    botSettingsAvailable = supportsBotSettings;
                    botKeySyncAvailable = supportsBotKeySync;
                    authenticating = false;
                    reconnectDelay = 2000;
                    if (firstLogin) {
                        restorePendingTexts();
                        connectionStatus = "";
                        showHome(false);
                    } else {
                        showConnectionStatus("");
                        if (active != null && !active.group()) {
                            MessagePushService.markRead(this, ownCode, active.code());
                            requestHistory(active, "");
                        } else if (active == null && !settings) {
                            renderContacts(false);
                            if (friendRows != null) refreshFriends();
                        }
                    }
                    refreshContacts.run();
                    if (groupsAvailable) refreshGroups(true);
                    syncNotifications(false);
                    openNotificationChat();
                    if (pendingImageUri != null) {
                        Uri image = pendingImageUri;
                        pendingImageUri = null;
                        sendImage(image);
                    }
                    if (pendingFileUri != null) {
                        Uri file = pendingFileUri;
                        pendingFileUri = null;
                        sendFile(file);
                    }
                });
                while (true) {
                    JSONObject packet = session.receive();
                    if (session.completeRequest(packet)) continue;
                    main.post(() -> {
                        if (connection != session) return;
                        try {
                            receive(packet);
                        } catch (JSONException | IOException error) {
                            disconnect();
                            showConnectionStatus("服务器消息处理失败 · " + error.getMessage());
                        }
                    });
                }
            } catch (IOException | JSONException error) {
                main.post(() -> {
                    if (isDestroyed() || generation != attempt) return;
                    disconnect();
                    if (error instanceof ChatConnection.AuthenticationException) {
                        syncNotificationsBeforeLogout();
                        clearSession();
                        forgetAccount();
                        showLogin("登录已失效，请重新登录 · " + error.getMessage());
                    } else if (error instanceof JSONException) {
                        showConnectionStatus("服务器登录响应无效 · " + error.getMessage());
                    } else if (savedAccount != null && foreground) {
                        showConnectionStatus("连接中断，正在重连…");
                        main.postDelayed(reconnect, reconnectDelay);
                        reconnectDelay = Math.min(reconnectDelay * 2, 30000);
                    } else {
                        showLogin("连接失败 · " + error.getMessage());
                    }
                });
            }
        }, "LAN Chat reader").start();
    }

    private static boolean supports(JSONArray values, String capability) throws JSONException {
        for (int index = 0; index < values.length(); index++) {
            if (capability.equals(values.getString(index))) return true;
        }
        return false;
    }

    private void showConnectionStatus(String status) {
        connectionStatus = status;
        if (ownCode == null) {
            showLogin(status);
        } else {
            connectionBanner.setText(status);
            connectionBanner.setVisibility(status.isEmpty() ? View.GONE : View.VISIBLE);
        }
    }

    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        openNotificationChat();
    }

    private void openNotificationChat() {
        if (connection == null) return;
        Uri route = getIntent().getData();
        if (route == null || !"lanchat".equals(route.getScheme())) return;
        getIntent().setData(null);
        List<String> parts = route.getPathSegments();
        if (parts.size() != 2 || !ownCode.equalsIgnoreCase(parts.get(0))) return;
        String peer = parts.get(1);
        if ("group".equals(route.getAuthority()) && MessagePushService.validGroupId(peer)) {
            MessagePushService.clearGroup(this, ownCode, peer);
            if (groupsAvailable) {
                socialRequest("group_detail", response -> {
                    Group group = readGroup(response.getJSONArray("groups").getJSONObject(0));
                    groupDirectory.put(key(peer), group);
                    openChat(group.contact());
                }, "group_id", peer);
            }
            return;
        }
        if (!"dm".equals(route.getAuthority()) || !MessagePushService.validUserCode(peer)) return;
        Contact contact = contacts.get(key(peer));
        if (contact != null) openChat(contact);
        else send("dm_lookup", "target_user_code", peer, "command_id", UUID.randomUUID().toString());
    }

    private void receive(JSONObject packet) throws JSONException, IOException {
        switch (packet.getString("type")) {
            case "dm_contacts" -> {
                JSONArray users = packet.optJSONArray("user_details");
                boolean changed = false;
                if (users != null) {
                    for (int i = 0; i < users.length(); i++) {
                        JSONObject user = users.getJSONObject(i);
                        String code = user.getString("user_code");
                        Contact contact = new Contact(code, user.getString("username"));
                        Contact prior = contacts.put(key(code), contact);
                        changed |= !contact.equals(prior);
                    }
                }
                if (changed && active == null && !settings) renderContacts(false);
            }
            case "dm_lookup" -> {
                String code = packet.getString("user_code");
                Contact contact = new Contact(code, packet.getString("username"));
                contacts.put(key(code), contact);
                openChat(contact);
            }
            case "private_chat", "offline_message", "dm_edited" -> {
                String author = packet.getString("user_code");
                boolean self = ownCode.equalsIgnoreCase(author);
                String peer = self ? packet.getString("target_user_code") : author;
                ChatMessage row = message(packet);
                if (self) confirmText(row.id());
                contacts.putIfAbsent(key(peer), new Contact(peer, self ? peer : packet.getString("username")));
                boolean live = packet.getString("type").equals("private_chat");
                boolean fresh = addMessage(peer, row, live);
                if (fresh && !self && !isActive(peer)
                        && (live || packet.getString("type").equals("offline_message"))) {
                    MessagePushService.markUnread(this, ownCode, peer, row.id());
                }
                if (!self && live) {
                    if (fresh && (!foreground || !isActive(peer))) MessagePushService.show(this, ownCode, author, false);
                    send("dm_received", "message_id", packet.getString("message_id"), "target_user_code", author);
                }
                if (active == null && !settings) renderContacts(false);
            }
            case "group_message" -> {
                String group = packet.getString("group_id");
                if (!MessagePushService.validGroupId(group)) throw new JSONException("服务器返回了无效的群编号");
                ChatMessage row = message(packet);
                boolean self = ownCode.equalsIgnoreCase(row.author());
                if (self) confirmText(row.id());
                if (addMessage(group, row, true) && !self && !isActive(group)) {
                    MessagePushService.markUnread(this, ownCode, group, messageKey(row));
                    MessagePushService.show(this, ownCode, group, true);
                    if (groupDirectory.containsKey(key(group))) {
                        renderGroups();
                        renderContacts(false);
                    } else {
                        refreshGroups(false);
                    }
                }
            }
            case "message_recalled" -> {
                String recalledId = packet.getString("message_id");
                confirmText(recalledId);
                boolean changed = false;
                boolean activeChanged = false;
                for (Map.Entry<String, Map<String, ChatMessage>> entry : histories.entrySet()) {
                    Map<String, ChatMessage> history = entry.getValue();
                    for (String identifier : new ArrayList<>(history.keySet())) {
                        ChatMessage prior = history.get(identifier);
                        if (prior == null || prior.recalled() || !prior.id().equals(recalledId)) continue;
                        history.put(identifier, prior.withRecalled());
                        changed = true;
                        if (active != null && key(active.code()).equals(entry.getKey())) activeChanged = true;
                    }
                }
                if (selectedQuote != null && selectedQuote.id().equals(recalledId)) {
                    selectedQuote = null;
                    quoteBar.setVisibility(View.GONE);
                }
                if (activeChanged) {
                    renderMessages(false);
                } else if (changed && active == null && !settings) {
                    renderContacts(false);
                }
            }
            case "history_response" -> {
                if (!packet.optString("search_query").isEmpty()) {
                    if (chatSearch != null) chatSearch.receive(packet);
                    return;
                }
                String peer = packet.getString(packet.has("group_id") ? "group_id" : "target_user_code");
                String peerKey = key(peer);
                String before = pendingHistory.remove(peerKey);
                boolean prepend = before != null && !before.isEmpty();
                JSONArray history = packet.optJSONArray("messages");
                Map<String, ChatMessage> merged = histories.computeIfAbsent(peerKey, ignored -> new LinkedHashMap<>());
                String previousCursor = historyCursor.get(peerKey);
                int added = 0;
                boolean changed = false;
                if (history != null) {
                    for (int i = 0; i < history.length(); i++) {
                        ChatMessage row = message(history.getJSONObject(i));
                        if (ownCode.equalsIgnoreCase(row.author())) confirmText(row.id());
                        String id = messageKey(row);
                        ChatMessage prior = merged.get(id);
                        ChatMessage next = merged.merge(id, row, MainActivity::mergeMessage);
                        if (prior == null) added++;
                        changed |= !next.equals(prior);
                    }
                }
                if (initializedHistory.add(peerKey) || prepend) {
                    if (packet.optBoolean("has_more") && history != null && history.length() > 0) {
                        historyCursor.put(peerKey, history.getJSONObject(0).getString("message_id"));
                    } else {
                        historyCursor.remove(peerKey);
                    }
                }
                if (isActive(peer)) {
                    if (prepend) visibleHistoryLimit += added;
                    if (changed || !Objects.equals(previousCursor, historyCursor.get(peerKey))) renderMessages(false, prepend);
                    else loadNextImage();
                } else if (changed && active == null && !settings) {
                    renderContacts(false);
                }
            }
            case "dm_backlog_end" -> send("dm_ack_batch", "message_id", packet.getString("message_id"));
            case "delivery_receipt" -> {
                String id = packet.getString("message_id");
                String delivery = packet.getString("delivery_state");
                if ("sent".equals(delivery) || "delivered".equals(delivery)) confirmText(id);
                String identifier = key(ownCode) + ":" + id;
                for (Map<String, ChatMessage> history : histories.values()) {
                    ChatMessage prior = history.get(identifier);
                    if (prior == null) continue;
                    ChatMessage updated = prior.withDelivery(delivery);
                    history.put(identifier, updated);
                    TextView time = visibleMessageTimes.get(identifier);
                    if (time != null) setMessageStatus(time, updated, true);
                }
            }
            case "dm_image_get" -> receiveImage(packet);
            case "friends_changed" -> {
                if (friendRows != null) refreshFriends();
            }
            case "groups_changed" -> {
                refreshGroups(false, packet.optString("group_id"));
            }
            case "error" -> {
                String command = packet.optString("command_id");
                failText(packet.optString("message_id"));
                if (download != null && download.command().equals(command)) {
                    File failed = cachedImage(download.peer(), download.message());
                    download = null;
                    missingImages.remove(failed);
                    ImageTile tile = visibleImages.get(failed);
                    if (tile != null) tile.status().setText("图片加载失败");
                    loadNextImage();
                }
                if (command.startsWith("history:")) pendingHistory.remove(command.substring("history:".length()));
                String detail = packet.getString("content");
                if (chatSearch == null || !chatSearch.error(command, detail)) {
                    toast(detail);
                }
            }
        }
    }

    private ChatMessage message(JSONObject packet) throws JSONException {
        int size = packet.optInt("image_size", 0);
        if (size < 0 || size > MAX_IMAGE) throw new JSONException("图片大小无效");
        String timestamp = packet.optString("created_at");
        Instant time;
        try {
            time = timestamp.isEmpty() ? null : Instant.parse(timestamp);
        } catch (DateTimeParseException error) {
            throw new JSONException("消息发送时间不是有效的 UTC 时间");
        }
        boolean recalled = packet.optBoolean("recalled");
        ChatConnection.Attachment attachment = !recalled && packet.has("attachment_id")
                ? ChatConnection.Attachment.read(packet) : null;
        String author = packet.getString("user_code");
        return new ChatMessage(packet.getString("message_id"), author, packet.optString("username", author),
                recalled ? RECALLED_TEXT : packet.getString("content"), recalled ? 0 : size, time,
                packet.optString("delivery_state"), packet.optLong("revision"), recalled, attachment);
    }

    private static ChatMessage mergeMessage(ChatMessage prior, ChatMessage received) {
        ChatMessage latest = prior.recalled() || (!received.recalled() && prior.revision() > received.revision())
                ? prior : received;
        if ("delivered".equals(prior.delivery()) || "delivered".equals(received.delivery())) {
            return latest.withDelivery("delivered");
        }
        return latest;
    }

    private boolean addMessage(String peer, ChatMessage row, boolean live) {
        Map<String, ChatMessage> history = histories.computeIfAbsent(key(peer), ignored -> new LinkedHashMap<>());
        ChatMessage last = isActive(peer) ? history.values().stream().max(MESSAGE_ORDER).orElse(null) : null;
        ChatMessage previous = history.get(messageKey(row));
        ChatMessage current = history.merge(messageKey(row), row, MainActivity::mergeMessage);
        if (!isActive(peer)) return previous == null;
        if (previous == null && (last == null || MESSAGE_ORDER.compare(row, last) > 0)) {
            if (history.size() > visibleHistoryLimit) visibleHistoryLimit++;
            appendMessage(row, live);
            scroll.followArrivals(live);
        } else if (!current.equals(previous)) {
            renderMessages(false);
        }
        return previous == null;
    }

    private void showHome(boolean directory) {
        // 切换外观主题会重建 Activity；重建后直接回到设置页，避免跳回会话列表。
        if (reopenSettingsAfterRecreate) {
            reopenSettingsAfterRecreate = false;
            showSettings();
            return;
        }
        boolean returningFromChat = active != null;
        releaseChatPanels();
        socialBack = null;
        groupDetails = null;
        friendRows = null;
        groupRows = null;
        settings = false;
        active = null;
        visibleImages.clear();
        missingImages.clear();
        visibleMessageTimes.clear();
        page = newPage(returningFromChat ? -1 : 0);
        LinearLayout header = bar(directory ? "联系人" : "会话");
        TextView heading = (TextView) header.getChildAt(0);
        if (socialAvailable) {
            TextView profile = avatar(username);
            profile.setBackground(ripple(surface(TINT, 24), false));
            profile.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_YES);
            profile.setContentDescription("我的主页");
            profile.setTooltipText("我的主页");
            profile.setOnClickListener(view -> openMyProfile());
            header.addView(profile, 0, new LinearLayout.LayoutParams(dp(48), dp(48)));
        }
        header.addView(iconButton("设置", R.drawable.ic_settings, false, this::showSettings));
        page.addView(header);

        FrameLayout panels = new FrameLayout(this);
        ScrollView conversations = new ScrollView(this);
        conversations.setVerticalScrollBarEnabled(false);
        contactRows = column();
        conversations.addView(contactRows, new ScrollView.LayoutParams(-1, -2));
        panels.addView(conversations, new FrameLayout.LayoutParams(-1, -1));
        ScrollView contactsPane = new ScrollView(this);
        contactsPane.setVerticalScrollBarEnabled(false);
        LinearLayout addressBook = column();
        contactsPane.addView(addressBook, new ScrollView.LayoutParams(-1, -2));
        panels.addView(contactsPane, new FrameLayout.LayoutParams(-1, -1));
        LinearLayout.LayoutParams space = new LinearLayout.LayoutParams(-1, 0, 1);
        space.topMargin = dp(12);
        page.addView(panels, space);

        FrameLayout dock = new FrameLayout(this);
        dock.setBackground(card(WHITE, 20));
        View highlight = new View(this);
        highlight.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        highlight.setBackground(surface(TINT, 17));
        dock.addView(highlight, new FrameLayout.LayoutParams(dp(64), dp(34), Gravity.LEFT | Gravity.CENTER_VERTICAL));
        LinearLayout actions = new LinearLayout(this);
        actions.setGravity(Gravity.CENTER_VERTICAL);
        ImageButton conversationTab = iconButton("会话", R.drawable.ic_private_chat, false, () -> {});
        ImageButton contactsTab = iconButton("联系人", R.drawable.ic_friends, false, () -> {});
        conversationTab.setBackgroundColor(Color.TRANSPARENT);
        contactsTab.setBackgroundColor(Color.TRANSPARENT);
        actions.addView(conversationTab, new LinearLayout.LayoutParams(0, dp(48), 1));
        actions.addView(contactsTab, new LinearLayout.LayoutParams(0, dp(48), 1));
        dock.addView(actions, new FrameLayout.LayoutParams(-1, -1));
        LinearLayout.LayoutParams dockPosition = new LinearLayout.LayoutParams(-1, dp(56));
        dockPosition.topMargin = dp(10);
        page.addView(dock, dockPosition);

        Consumer<Boolean> moveHighlight = animate -> {
            View selected = directorySelected ? contactsTab : conversationTab;
            float target = (selected.getLeft() + selected.getRight() - highlight.getWidth()) / 2f;
            highlight.animate().cancel();
            if (animate) {
                highlight.animate().translationX(target).setDuration(MOTION_OPEN).setInterpolator(ARRIVE).start();
            } else {
                highlight.setTranslationX(target);
            }
        };
        actions.addOnLayoutChangeListener((view, l, t, r, b, oldL, oldT, oldR, oldB) -> {
            if (r - l != oldR - oldL) moveHighlight.accept(false);
        });
        Consumer<Boolean> select = showDirectory -> {
            if (actions.isLaidOut() && directorySelected == showDirectory) return;
            directorySelected = showDirectory;
            heading.setText(showDirectory ? "联系人" : "会话");
            conversations.setVisibility(showDirectory ? View.GONE : View.VISIBLE);
            contactsPane.setVisibility(showDirectory ? View.VISIBLE : View.GONE);
            conversationTab.setSelected(!showDirectory);
            contactsTab.setSelected(showDirectory);
            conversationTab.setImageTintList(ColorStateList.valueOf(showDirectory ? MUTED : ACCENT));
            contactsTab.setImageTintList(ColorStateList.valueOf(showDirectory ? ACCENT : MUTED));
            socialBack = showDirectory ? conversationTab::performClick : null;
            updateSystemBack();
            if (actions.isLaidOut()) moveHighlight.accept(true);
            if (showDirectory && addressBook.getChildCount() == 0) populateDirectory(addressBook);
        };
        conversationTab.setOnClickListener(view -> select.accept(false));
        contactsTab.setOnClickListener(view -> select.accept(true));
        select.accept(directory);
        renderContacts(!directory && !returningFromChat);
    }

    private void restoreHome() {
        showHome(directorySelected);
    }

    private void tabAction(LinearLayout row, View control) {
        LinearLayout.LayoutParams width = new LinearLayout.LayoutParams(0, -2, 1);
        if (row.getChildCount() > 0) width.setMarginStart(dp(6));
        row.addView(control, width);
    }

    private record Profile(String code, String name, String bio, String friendship) {
        static Profile read(JSONObject value) throws JSONException {
            return new Profile(value.getString("user_code"), value.getString("display_name"),
                    value.getString("bio"), value.getString("friendship"));
        }
    }

    private interface SocialReply {
        void accept(JSONObject reply) throws JSONException;
    }

    private void socialRequest(String type, SocialReply reply, Object... fields) {
        socialRequest(page, type, reply, fields);
    }

    private void socialRequest(LinearLayout source, String type, SocialReply reply, Object... fields) {
        ChatConnection session = connection;
        if (session == null) {
            toast("连接尚未恢复，请稍后重试");
            return;
        }
        socialRequests.execute(() -> {
            try {
                JSONObject response = session.request(packet(type, fields));
                main.post(() -> {
                    if (isDestroyed() || connection != session || (source != null && page != source)) return;
                    try {
                        reply.accept(response);
                    } catch (JSONException error) {
                        toast("服务器响应无效 · " + error.getMessage());
                    }
                });
            } catch (IOException | JSONException error) {
                main.post(() -> {
                    if (!isDestroyed() && connection == session && (source == null || page == source)) {
                        toast(error.getMessage());
                    }
                });
            }
        });
    }

    private LinearLayout socialPage(String title, Runnable back, boolean slideFromLeft) {
        releaseChatPanels();
        active = null;
        groupDetails = null;
        settings = false;
        contactRows = null;
        friendRows = null;
        groupRows = null;
        socialBack = back;
        page = newPage(slideFromLeft ? -1 : 1, slideFromLeft);
        LinearLayout header = bar(title);
        header.addView(iconButton("返回", R.drawable.ic_back, false, back), 0);
        page.addView(header);
        ScrollView pane = new ScrollView(this);
        pane.setVerticalScrollBarEnabled(false);
        LinearLayout body = column();
        pane.addView(body, new ScrollView.LayoutParams(-1, -2));
        page.addView(pane, new LinearLayout.LayoutParams(-1, 0, 1));
        return body;
    }

    private void populateDirectory(LinearLayout body) {
        addSpaced(body, button("发起私聊", false, this::chooseContact), 0);
        if (socialAvailable) {
            directoryHeading(body, "好友", "添加好友", this::findFriend);
            friendRows = column();
            addSpaced(body, friendRows, 4);
            refreshFriends();
        }
        if (groupsAvailable) {
            directoryHeading(body, "群聊", "创建群聊", this::chooseGroupMembers);
            groupRows = column();
            addSpaced(body, groupRows, 4);
            if (groupDirectory.isEmpty()) refreshGroups(false);
            else renderGroups();
        }
    }

    private void directoryHeading(LinearLayout body, String name, String action, Runnable click) {
        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.addView(title(name), new LinearLayout.LayoutParams(0, -2, 1));
        row.addView(button(action, false, click));
        addSpaced(body, row, 14);
    }

    private static Group readGroup(JSONObject value) throws JSONException {
        String id = value.getString("id");
        if (!MessagePushService.validGroupId(id)) throw new JSONException("服务器返回了无效的群编号");
        return new Group(new Contact(id, value.getString("name"), true), value.getInt("member_count"),
                value.optString("role"), value.optString("announcement"), value.optString("invited_by_user_code"),
                value.optLong("joined_after"));
    }

    private void refreshGroups(boolean reconnected) {
        refreshGroups(reconnected, "");
    }

    private void refreshGroups(boolean reconnected, String affected) {
        socialRequest(null, "group_list", response -> {
            Map<String, Group> refreshed = new LinkedHashMap<>();
            JSONArray groups = response.optJSONArray("groups");
            if (groups != null) {
                for (int i = 0; i < groups.length(); i++) {
                    Group group = readGroup(groups.getJSONObject(i));
                    refreshed.put(key(group.contact().code()), group);
                }
            }
            Set<String> changedMemberships = new HashSet<>();
            for (Map.Entry<String, Group> entry : groupDirectory.entrySet()) {
                Group current = refreshed.get(entry.getKey());
                if (current == null || current.joinedAfter() != entry.getValue().joinedAfter()) {
                    changedMemberships.add(entry.getKey());
                }
            }
            groupDirectory.clear();
            groupDirectory.putAll(refreshed);
            for (String group : changedMemberships) {
                if (groupDirectory.containsKey(group)) clearGroupHistory(group);
                else forgetGroup(group);
            }
            if (MessagePushService.validGroupId(affected) && !refreshed.containsKey(key(affected))) {
                MessagePushService.clearGroup(this, ownCode, affected);
            }
            if (active != null && active.group()) {
                Group current = refreshed.get(key(active.code()));
                if (current != null && !active.equals(current.contact())) openChat(current.contact());
                else if (current != null && (reconnected || changedMemberships.contains(key(active.code())))) {
                    renderMessages(false, false);
                    requestHistory(active, "");
                }
            }
            if (groupDetails != null && (reconnected || groupDetails.code().equals(affected))) {
                showGroupDetails(groupDetails);
            }
            renderGroups();
            renderContacts(false);
        });
    }

    private void renderGroups() {
        if (groupRows == null) return;
        groupRows.removeAllViews();
        if (groupDirectory.isEmpty()) {
            addSpaced(groupRows, label("还没有群聊", 14, MUTED), 12);
        }
        for (Group group : groupDirectory.values()) {
            Contact contact = group.contact();
            String summary = group.memberCount() + " 位成员";
            int unread = MessagePushService.unreadCount(this, ownCode, contact.code());
            if (unread > 0) summary += " · " + unread + " 条未读";
            addSpaced(groupRows, personCard(contact.name(), summary, () -> openChat(contact)), 8);
        }
    }

    private void forgetGroup(String group) {
        groupDirectory.remove(group);
        clearGroupHistory(group);
        if (isActive(group) || (groupDetails != null && groupDetails.code().equals(group))) showHome(true);
    }

    private void clearGroupHistory(String group) {
        for (String id : new ArrayList<>(pendingTexts.keySet())) {
            if (group.equals(key(pendingTexts.get(id).peer().code()))) confirmText(id);
        }
        histories.remove(group);
        initializedHistory.remove(group);
        pendingHistory.remove(group);
        historyCursor.remove(group);
        if (download != null && group.equals(key(download.peer().code()))) download = null;
        MessagePushService.clearGroup(this, ownCode, group);
    }

    private void chooseGroupMembers() {
        socialRequest("friend_list", response -> {
            JSONArray values = response.optJSONArray("profiles");
            ArrayList<Profile> friends = new ArrayList<>();
            if (values != null) {
                for (int i = 0; i < values.length(); i++) {
                    Profile profile = Profile.read(values.getJSONObject(i));
                    if ("friend".equals(profile.friendship())) friends.add(profile);
                }
            }
            if (friends.isEmpty()) {
                toast("请先添加好友");
                return;
            }
            ScrollView content = new ScrollView(this);
            LinearLayout body = column();
            body.setPadding(dp(18), dp(12), dp(18), 0);
            content.addView(body);
            EditText name = field("群名称", InputType.TYPE_CLASS_TEXT);
            name.setSingleLine(true);
            body.addView(name);
            ArrayList<android.widget.CheckBox> choices = new ArrayList<>();
            for (Profile friend : friends) {
                android.widget.CheckBox option = new android.widget.CheckBox(this);
                option.setText(friend.name().equals(friend.code()) ? friend.name() : friend.name() + " · " + friend.code());
                option.setTextColor(INK);
                option.setMinHeight(dp(48));
                choices.add(option);
                body.addView(option);
            }
            AlertDialog dialog = new AlertDialog.Builder(this).setTitle("创建群聊").setView(content)
                    .setNegativeButton("取消", null).setPositiveButton("创建", null).create();
            dialog.setOnShowListener(ignored -> dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view -> {
                String title = name.getText().toString().trim();
                JSONArray members = new JSONArray();
                for (int i = 0; i < choices.size(); i++) {
                    if (choices.get(i).isChecked()) members.put(friends.get(i).code());
                }
                if (title.isEmpty() || title.codePointCount(0, title.length()) > 32) {
                    name.setError("群名需要 1–32 字");
                    return;
                }
                if (members.length() < 1 || members.length() > 19) {
                    toast("请选择 1–19 位好友");
                    return;
                }
                dialog.dismiss();
                socialRequest("group_create", created -> {
                    Group group = readGroup(created.getJSONArray("groups").getJSONObject(0));
                    groupDirectory.put(key(group.contact().code()), group);
                    openChat(group.contact());
                }, "room", title, "users", members);
            }));
            dialog.show();
        });
    }

    private void showGroupDetails(Contact group) {
        socialRequest("group_detail", response -> {
            Group details = readGroup(response.getJSONArray("groups").getJSONObject(0));
            Contact current = details.contact();
            groupDirectory.put(key(current.code()), details);
            boolean owner = "owner".equals(details.role());
            boolean administer = groupManagementAvailable && (owner || "admin".equals(details.role()));
            JSONArray members = response.getJSONArray("profiles");
            LinearLayout body = socialPage(current.name(), () -> openChat(current), false);
            groupDetails = current;
            if (groupManagementAvailable) {
                if (!details.invitedBy().isEmpty()) {
                    addSpaced(body, label("由 " + details.invitedBy() + " 邀请加入", 14, MUTED), 12);
                }
                if (!details.announcement().isEmpty()) {
                    addSpaced(body, label("群公告", 14, MUTED), 12);
                    addSpaced(body, label(details.announcement(), 16, INK), 4);
                }
                if (administer) {
                    if (members.length() < 20) {
                        addSpaced(body, button("邀请好友", false, () -> inviteGroupFriend(current, members)), 10);
                    }
                    addSpaced(body, button("编辑群公告", false,
                            () -> editGroupSetting(current, true, details.announcement())), 8);
                }
                if (owner) addSpaced(body, button("修改群名", false,
                        () -> editGroupSetting(current, false, current.name())), 8);
            }
            addSpaced(body, label(members.length() + " 位成员", 14, MUTED), 12);
            for (int i = 0; i < members.length(); i++) {
                JSONObject member = members.getJSONObject(i);
                String code = member.getString("user_code");
                String name = member.getString("display_name");
                String role = member.optString("role");
                String summary = switch (role) {
                    case "owner" -> code + " · 群主";
                    case "admin" -> code + " · 管理员";
                    default -> code;
                };
                boolean manageMember = administer && !ownCode.equalsIgnoreCase(code) && !"owner".equals(role)
                        && (owner || !"admin".equals(role));
                addSpaced(body, personCard(name, summary, manageMember
                        ? () -> showGroupMemberActions(current, name, code, role, owner)
                        : () -> openProfile(code, () -> showGroupDetails(current))), 8);
            }
            if (owner && groupManagementAvailable) {
                addSpaced(body, label("转让群主后可退出群聊", 14, MUTED), 14);
            } else {
                addSpaced(body, button("退出群聊", false, () -> new AlertDialog.Builder(this)
                        .setTitle("退出「" + current.name() + "」？").setNegativeButton("取消", null)
                        .setPositiveButton("退出", (dialog, which) -> socialRequest("group_leave", left -> {
                            forgetGroup(key(current.code()));
                        }, "group_id", current.code())).show()), 14);
            }
        }, "group_id", group.code());
    }

    private void inviteGroupFriend(Contact group, JSONArray members) {
        socialRequest("friend_list", response -> {
            Set<String> present = new HashSet<>();
            for (int index = 0; index < members.length(); index++) {
                present.add(key(members.getJSONObject(index).getString("user_code")));
            }
            ArrayList<Profile> choices = new ArrayList<>();
            JSONArray profiles = response.optJSONArray("profiles");
            if (profiles != null) {
                for (int index = 0; index < profiles.length(); index++) {
                    Profile friend = Profile.read(profiles.getJSONObject(index));
                    if ("friend".equals(friend.friendship()) && !present.contains(key(friend.code()))) choices.add(friend);
                }
            }
            if (choices.isEmpty()) {
                toast("没有可以邀请的好友");
                return;
            }
            String[] labels = choices.stream().map(friend -> friend.name() + " · " + friend.code()).toArray(String[]::new);
            new AlertDialog.Builder(this).setTitle("邀请好友").setNegativeButton("取消", null)
                    .setItems(labels, (dialog, index) -> groupAction(group, "group_invite", choices.get(index).code(), ""))
                    .show();
        });
    }

    private void showGroupMemberActions(Contact group, String name, String code, String role, boolean owner) {
        Map<String, Runnable> actions = new LinkedHashMap<>();
        actions.put("查看个人主页", () -> openProfile(code, () -> showGroupDetails(group)));
        actions.put("移出群聊", () -> confirmGroupAction("将「" + name + "」移出群聊？", "移出",
                () -> groupAction(group, "group_kick", code, "")));
        if (owner) {
            boolean admin = "admin".equals(role);
            actions.put(admin ? "取消管理员" : "设为管理员",
                    () -> groupAction(group, "group_role", code, admin ? "member" : "admin"));
            actions.put("转让群主", () -> confirmGroupAction("将群主转让给「" + name + "」？", "转让",
                    () -> groupAction(group, "group_transfer", code, "")));
        }
        String[] labels = actions.keySet().toArray(new String[0]);
        new AlertDialog.Builder(this).setTitle(name).setNegativeButton("取消", null)
                .setItems(labels, (dialog, index) -> actions.get(labels[index]).run()).show();
    }

    private void confirmGroupAction(String title, String action, Runnable confirm) {
        new AlertDialog.Builder(this).setTitle(title).setNegativeButton("取消", null)
                .setPositiveButton(action, (dialog, which) -> confirm.run()).show();
    }

    private void editGroupSetting(Contact group, boolean announcement, String value) {
        String title = announcement ? "群公告" : "群名称";
        LinearLayout form = column();
        form.setPadding(dp(18), dp(12), dp(18), 0);
        EditText input = field(title, InputType.TYPE_CLASS_TEXT);
        input.setSingleLine(true);
        input.setText(value);
        form.addView(input);
        AlertDialog dialog = new AlertDialog.Builder(this).setTitle("编辑" + title).setView(form)
                .setNegativeButton("取消", null).setPositiveButton("保存", null).create();
        dialog.setOnShowListener(ignored -> dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view -> {
            String text = input.getText().toString().trim();
            if (text.codePointCount(0, text.length()) > (announcement ? 300 : 32) || (!announcement && text.isEmpty())) {
                input.setError(announcement ? "群公告最多 300 字" : "群名需要 1–32 字");
                return;
            }
            dialog.dismiss();
            groupAction(group, announcement ? "group_announce" : "group_update", "", text);
        }));
        dialog.show();
    }

    private void groupAction(Contact group, String type, String member, String value) {
        socialRequest(type, response -> showGroupDetails(group),
                "group_id", group.code(), "target_user_code", member, "content", value);
    }

    private void refreshFriends() {
        socialRequest("friend_list", response -> {
            friendRows.removeAllViews();
            JSONArray values = response.optJSONArray("profiles");
            if (values == null || values.length() == 0) {
                addSpaced(friendRows, label("还没有好友", 14, MUTED), 12);
                return;
            }
            for (int index = 0; index < values.length(); index++) {
                Profile profile = Profile.read(values.getJSONObject(index));
                String secondary = switch (profile.friendship()) {
                    case "incoming" -> "好友申请";
                    case "outgoing" -> "等待确认";
                    default -> profile.code();
                };
                addSpaced(friendRows, personCard(profile.name(), secondary,
                        () -> openProfile(profile.code(), this::restoreHome)), 7);
            }
        });
    }

    private View personCard(String name, String secondary, Runnable action) {
        LinearLayout entry = new LinearLayout(this);
        entry.setGravity(Gravity.CENTER_VERTICAL);
        entry.setPadding(dp(14), dp(12), dp(14), dp(15));
        entry.setBackground(ripple(bookCard(17), false));
        entry.addView(avatar(name), new LinearLayout.LayoutParams(dp(44), dp(44)));
        LinearLayout details = column();
        details.setPadding(dp(12), 0, 0, 0);
        TextView heading = label(name, 16, INK);
        heading.setTypeface(mediumFont);
        details.addView(heading);
        addSpaced(details, label(secondary, 12, MUTED), 3);
        entry.addView(details, new LinearLayout.LayoutParams(0, -2, 1));
        entry.setOnClickListener(view -> action.run());
        return entry;
    }

    private void findFriend() {
        EditText code = field("对方的用户代码", InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        code.setSingleLine(true);
        LinearLayout body = column();
        body.setPadding(dp(18), dp(12), dp(18), 0);
        body.addView(code);
        AlertDialog dialog = new AlertDialog.Builder(this).setTitle("添加好友").setView(body)
                .setNegativeButton("取消", null).setPositiveButton("查找", null).create();
        dialog.setOnShowListener(ignored -> dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view -> {
            String target = code.getText().toString().trim();
            if (!MessagePushService.validUserCode(target) || ownCode.equalsIgnoreCase(target)) {
                code.setError("请输入其他用户的有效代码");
                return;
            }
            socialRequest("profile_get", result -> {
                dialog.dismiss();
                showProfile(Profile.read(result.getJSONObject("profile")), this::restoreHome, false);
            }, "target_user_code", target);
        }));
        dialog.show();
    }

    private void openProfile(String code, Runnable back) {
        socialRequest("profile_get", result -> showProfile(Profile.read(result.getJSONObject("profile")), back, false),
                "target_user_code", code);
    }

    private void openMyProfile() {
        socialRequest("profile_get",
                result -> showProfile(Profile.read(result.getJSONObject("profile")), this::restoreHome, true));
    }

    private void showProfile(Profile profile, Runnable back, boolean slideFromLeft) {
        LinearLayout body = socialPage("个人主页", back, slideFromLeft);
        LinearLayout cover = column();
        cover.setPadding(dp(20), dp(22), dp(20), dp(26));
        cover.setBackground(bookCard(22));
        cover.addView(avatar(profile.name()), new LinearLayout.LayoutParams(dp(60), dp(60)));
        addSpaced(cover, title(profile.name()), 11);
        addSpaced(cover, label("用户代码  " + profile.code(), 13, MUTED), 5);
        if (!profile.bio().isEmpty()) addSpaced(cover, label(profile.bio(), 15, INK), 12);
        addSpaced(body, cover, 12);
        if ("self".equals(profile.friendship())) {
            addSpaced(body, button("编辑资料", true, () -> editProfile(profile, back)), 10);
            return;
        }
        LinearLayout actions = new LinearLayout(this);
        tabAction(actions, button("私聊", true, () -> {
            Contact peer = new Contact(profile.code(), profile.name());
            contacts.put(key(peer.code()), peer);
            openChat(peer);
        }));
        if ("none".equals(profile.friendship())) {
            tabAction(actions, button("添加好友", false, () -> socialRequest("friend_request",
                    response -> showProfile(Profile.read(response.getJSONObject("profile")), back, false),
                    "target_user_code", profile.code())));
        } else if ("incoming".equals(profile.friendship())) {
            tabAction(actions, button("同意申请", false, () -> respondToFriend(profile, back, "accept")));
            tabAction(actions, button("拒绝", false, () -> respondToFriend(profile, back, "decline")));
        } else if ("outgoing".equals(profile.friendship())) {
            Button pending = button("等待确认", false, () -> {});
            pending.setEnabled(false);
            tabAction(actions, pending);
        }
        addSpaced(body, actions, 10);
    }

    private void respondToFriend(Profile profile, Runnable back, String action) {
        socialRequest("friend_respond", response -> showProfile(Profile.read(response.getJSONObject("profile")), back, false),
                "target_user_code", profile.code(), "action", action);
    }

    private void editProfile(Profile profile, Runnable back) {
        LinearLayout form = column();
        form.setPadding(dp(18), dp(12), dp(18), 0);
        EditText name = field("昵称", InputType.TYPE_CLASS_TEXT);
        EditText bio = field("个人简介", InputType.TYPE_CLASS_TEXT);
        name.setSingleLine(true);
        bio.setSingleLine(true);
        name.setText(profile.name());
        bio.setText(profile.bio());
        form.addView(name);
        addSpaced(form, bio, 8);
        AlertDialog dialog = new AlertDialog.Builder(this).setTitle("编辑资料").setView(form)
                .setNegativeButton("取消", null).setPositiveButton("保存", null).create();
        dialog.setOnShowListener(ignored -> dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view -> {
            try {
                JSONObject changes = new JSONObject().put("display_name", name.getText().toString())
                        .put("bio", bio.getText().toString());
                socialRequest("profile_save", response -> {
                    dialog.dismiss();
                    showProfile(Profile.read(response.getJSONObject("profile")), back, false);
                }, "profile", changes);
            } catch (JSONException error) {
                name.setError(error.getMessage());
            }
        }));
        dialog.show();
    }

    private void renderContacts(boolean animate) {
        if (contactRows == null) return;
        boolean reveal = animate || (contactRows.getChildCount() == 1 && contactRows.getChildAt(0) instanceof TextView);
        contactRows.removeAllViews();
        List<Contact> conversations = new ArrayList<>(contacts.values());
        for (Group group : groupDirectory.values()) conversations.add(group.contact());
        if (conversations.isEmpty()) {
            TextView empty = label("还没有会话", 15, MUTED);
            empty.setGravity(Gravity.CENTER);
            empty.setPadding(dp(12), dp(42), dp(12), dp(42));
            contactRows.addView(empty);
        }
        int index = 0;
        for (Contact person : conversations) {
            LinearLayout entry = new LinearLayout(this);
            entry.setGravity(Gravity.CENTER_VERTICAL);
            entry.setPadding(dp(13), dp(12), dp(13), dp(12));
            entry.setMinimumHeight(dp(71));
            entry.setBackground(ripple(bookCard(17), false));
            entry.setFocusable(true);
            entry.addView(avatar(person.name()), new LinearLayout.LayoutParams(dp(44), dp(44)));

            LinearLayout identity = column();
            TextView name = label(person.name(), 16, INK);
            name.setTypeface(mediumFont);
            name.setMaxLines(1);
            name.setEllipsize(TextUtils.TruncateAt.END);
            identity.addView(name);
            ChatMessage latest = null;
            Map<String, ChatMessage> conversation = histories.get(key(person.code()));
            if (conversation != null) {
                latest = conversation.values().stream().max(MESSAGE_ORDER).orElse(null);
            }
            String preview = latest == null ? (person.group() ? "群聊" : person.code())
                    : latest.imageSize() > 0 ? "图片" : latest.text();
            TextView summary = label(preview, 13, MUTED);
            summary.setMaxLines(1);
            summary.setEllipsize(TextUtils.TruncateAt.END);
            addSpaced(identity, summary, 3);
            LinearLayout.LayoutParams identityPosition = new LinearLayout.LayoutParams(0, -2, 1);
            identityPosition.setMarginStart(dp(12));
            entry.addView(identity, identityPosition);
            if (latest != null) {
                TextView time = label(messageTime(latest, false), 12, MUTED);
                time.setPadding(dp(8), 0, 0, 0);
                entry.addView(time);
            }
            int unread = MessagePushService.unreadCount(this, ownCode, person.code());
            if (unread > 0) {
                TextView badge = label(unread > 99 ? "99+" : Integer.toString(unread), 12, WHITE);
                badge.setGravity(Gravity.CENTER);
                badge.setContentDescription(unread > 99 ? "超过 99 条未读消息" : unread + " 条未读消息");
                badge.setPadding(dp(6), dp(3), dp(6), dp(3));
                badge.setBackground(surface(ACCENT_FILL, 12));
                LinearLayout.LayoutParams badgePosition = new LinearLayout.LayoutParams(-2, -2);
                badgePosition.setMarginStart(dp(8));
                entry.addView(badge, badgePosition);
            }
            entry.setOnClickListener(view -> openChat(person));
            addSpaced(contactRows, entry, 6);
            if (reveal && index < 6) {
                entry.setAlpha(0f);
                entry.setTranslationY(-dp(12));
                entry.animate().alpha(1f).translationY(0f).setStartDelay(index * 40L)
                        .setDuration(MOTION_ENTER).setInterpolator(ARRIVE).start();
            }
            index++;
        }
    }

    private void chooseContact() {
        EditText code = field("对方的用户代码", InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        code.setSingleLine(true);
        LinearLayout body = column();
        body.setPadding(dp(22), dp(12), dp(22), 0);
        body.addView(code);
        new AlertDialog.Builder(this).setTitle("发起私信").setView(body).setNegativeButton("取消", null)
                .setPositiveButton("打开", (dialog, which) -> send("dm_lookup", "target_user_code",
                        code.getText().toString().trim(), "command_id", UUID.randomUUID().toString())).show();
    }

    private void openChat(Contact contact) {
        if (isActive(contact.code())) {
            scroll.jumpToLatest();
            return;
        }
        releaseChatPanels();
        socialBack = contact.group() || directorySelected ? () -> showHome(true) : null;
        friendRows = null;
        groupRows = null;
        groupDetails = null;
        active = contact;
        settings = false;
        selectedQuote = null;
        visibleHistoryLimit = INITIAL_HISTORY;
        revealedSearchMessage = null;
        if (contact.group()) MessagePushService.clearGroup(this, ownCode, contact.code());
        else MessagePushService.markRead(this, ownCode, contact.code());
        contactRows = null;
        page = newPage(1);
        LinearLayout header = bar(contact.name());
        header.addView(iconButton("返回", R.drawable.ic_back, false, () -> {
            if (!isActive(contact.code())) return;
            if (chatSearch.open) chatSearch.setOpen(false);
            else if (socialBack != null) socialBack.run();
            else showHome(false);
        }), 0);
        LinearLayout.LayoutParams iconPosition = new LinearLayout.LayoutParams(dp(36), dp(36));
        iconPosition.setMarginStart(dp(9));
        TextView identity = avatar(contact.name());
        if (contact.group()) {
            identity.setContentDescription("群聊详情");
            identity.setOnClickListener(view -> showGroupDetails(contact));
        } else if (socialAvailable) {
            identity.setContentDescription("查看 " + contact.name() + " 的个人主页");
            identity.setOnClickListener(view -> openProfile(contact.code(), () -> openChat(contact)));
        }
        header.addView(identity, 1, iconPosition);
        ImageButton search = iconButton("搜索", R.drawable.ic_search, false, () -> {
            if (isActive(contact.code())) chatSearch.setOpen(true);
        });
        header.addView(search);
        FrameLayout headerSlot = new FrameLayout(this);
        headerSlot.addView(header, new FrameLayout.LayoutParams(-1, -2));
        page.addView(headerSlot);

        chatBody = new FrameLayout(this);
        LinearLayout conversation = column();
        chatBody.addView(conversation, new FrameLayout.LayoutParams(-1, -1));
        page.addView(chatBody, new LinearLayout.LayoutParams(-1, 0, 1));
        chatSearch = new ChatSearch(contact, headerSlot, header, search, chatBody, conversation);

        FrameLayout messageArea = new FrameLayout(this);
        scroll = new ConversationScroll();
        scroll.setVerticalScrollBarEnabled(false);
        scroll.setClipToPadding(false);
        scroll.setPadding(0, dp(8), 0, dp(9));
        messages = column();
        messages.setClipChildren(false);
        scroll.addView(messages, new ScrollView.LayoutParams(-1, -2));
        messageArea.addView(scroll, new FrameLayout.LayoutParams(-1, -1));
        ImageButton latest = iconButton("回到最新消息", R.drawable.ic_jump_latest, false, scroll::jumpToLatest);
        latest.setBackground(ripple(card(WHITE, 24), false));
        latest.setVisibility(View.GONE);
        latest.setElevation(dp(2));
        scroll.latestButton = latest;
        FrameLayout.LayoutParams latestPosition = new FrameLayout.LayoutParams(dp(48), dp(48),
                Gravity.BOTTOM | Gravity.END);
        latestPosition.bottomMargin = dp(10);
        latestPosition.setMarginEnd(dp(6));
        messageArea.addView(latest, latestPosition);
        conversation.addView(messageArea, new LinearLayout.LayoutParams(-1, 0, 1));

        quoteBar = new LinearLayout(this);
        quoteBar.setGravity(Gravity.CENTER_VERTICAL);
        quoteBar.setPadding(dp(12), 0, dp(5), 0);
        quoteBar.setBackground(card(WHITE, 15));
        quotePreview = label("", 13, MUTED);
        quotePreview.setMaxLines(1);
        quotePreview.setEllipsize(TextUtils.TruncateAt.END);
        quoteBar.addView(quotePreview, new LinearLayout.LayoutParams(0, -2, 1));
        Button cancelQuote = button("×", false, () -> {
            selectedQuote = null;
            quoteBar.setVisibility(View.GONE);
        });
        cancelQuote.setContentDescription("取消引用");
        quoteBar.addView(cancelQuote);
        quoteBar.setVisibility(View.GONE);
        addSpaced(conversation, quoteBar, 6);

        LinearLayout composer = new LinearLayout(this);
        composer.setGravity(Gravity.BOTTOM);
        composer.setPadding(dp(5), dp(5), dp(5), dp(5));
        composer.setBackground(card(WHITE, 21));
        ImageButton attachment = iconButton(filesAvailable ? "附件" : "图片", R.drawable.ic_attachment,
                false, () -> {
                    if (!isActive(contact.code())) return;
                    if (filesAvailable) composerPanels.toggle(ComposerTool.ATTACHMENT);
                    else launchAttachmentPicker(contact, true);
                });
        attachment.setEnabled(imagesAvailable || filesAvailable);
        if (contact.group() && !groupAttachmentsAvailable) attachment.setVisibility(View.GONE);
        composer.addView(attachment);
        ImageButton emoji = iconButton("选择表情", R.drawable.ic_emoji, false, () -> {
            if (isActive(contact.code())) composerPanels.toggle(ComposerTool.EMOJI);
        });
        LinearLayout.LayoutParams emojiPosition = new LinearLayout.LayoutParams(dp(48), dp(48));
        emojiPosition.setMarginStart(dp(4));
        composer.addView(emoji, emojiPosition);
        draftInput = field("输入消息", InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE);
        draftInput.setGravity(Gravity.CENTER_VERTICAL);
        draftInput.setMaxLines(4);
        draftInput.setBackgroundColor(Color.TRANSPARENT);
        String draftKey = accountKey("draft", key(contact.code()));
        draftInput.setText(preferences().getString(draftKey, ""));
        draftInput.setOnTouchListener((view, event) -> {
            if (event.getActionMasked() == MotionEvent.ACTION_DOWN && isActive(contact.code())) composerPanels.hide();
            return false;
        });
        composer.addView(draftInput, new LinearLayout.LayoutParams(0, -2, 1));
        ImageButton sendButton = iconButton("发送", R.drawable.ic_send, true, () -> {
            // 输入法还在组字（拼音没上屏）时不能把原始拼音发出去：先让 IME 提交候选词。
            if (BaseInputConnection.getComposingSpanStart(draftInput.getText()) >= 0) {
                draftInput.setError(null);
                InputMethodManager manager = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
                if (manager != null) manager.viewClicked(draftInput);
                return;
            }
            String text = draftInput.getText().toString();
            if (text.trim().isEmpty()) return;
            if (selectedQuote != null) text = quoteLine(contact, selectedQuote) + "\n" + text;
            String id = UUID.randomUUID().toString();
            PendingText pending = new PendingText(contact, text, Instant.now());
            try {
                if (packet("private_chat", "content", text).toString().getBytes(StandardCharsets.UTF_8).length
                        > ChatConnection.MAX_TEXT_BYTES) {
                    draftInput.setError("消息太长，请分段发送");
                    return;
                }
                String stored = new JSONObject().put("peer", contact.code()).put("name", contact.name()).put("group", contact.group())
                        .put("text", text).put("time", pending.time().toString()).toString();
                if (!preferences().edit().putString(accountKey("outbox", id), stored).remove(draftKey).commit()) {
                    toast("无法保存待发送消息");
                    return;
                }
            } catch (JSONException error) {
                toast("无法保存待发送消息 · " + error.getMessage());
                return;
            }
            pendingTexts.put(id, pending);
            addMessage(contact.code(), new ChatMessage(id, ownCode, username, text, 0, pending.time(), "", 0, false, null), true);
            draftInput.setText("");
            selectedQuote = null;
            quoteBar.setVisibility(View.GONE);
            composerPanels.hide();
            sendPendingText(id);
        });
        sendButton.setEnabled(!draftInput.getText().toString().trim().isEmpty());
        draftInput.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) {
                sendButton.setEnabled(!s.toString().trim().isEmpty());
                // 组字中的拼音不写进草稿，否则重进会话会看到一串未上屏的拼音。
                if (s instanceof Spannable && BaseInputConnection.getComposingSpanStart((Spannable) s) >= 0) return;
                SharedPreferences.Editor drafts = preferences().edit();
                if (s.length() == 0) drafts.remove(draftKey);
                else drafts.putString(draftKey, s.toString());
                drafts.apply();
            }
            @Override public void afterTextChanged(Editable s) {}
        });
        composer.addView(sendButton);
        addSpaced(conversation, composer, 6);
        composerPanels = new ComposerPanels(contact, composer, draftInput, attachment, emoji);
        chatBody.addView(composerPanels, new FrameLayout.LayoutParams(-1, -1));
        renderMessages(true);
        requestHistory(contact, "");
    }

    private void releaseChatPanels() {
        if (chatSearch != null) {
            chatSearch.dispose();
            chatSearch = null;
        }
        if (composerPanels != null) {
            composerPanels.dispose();
            composerPanels = null;
        }
    }

    private void revealSearchMatch(Contact peer, ChatMessage row) {
        scroll.pauseFollowing();
        revealedSearchMessage = messageKey(row);
        addMessage(peer.code(), row, false);
        ScrollView current = scroll;
        if (messages.findViewWithTag(revealedSearchMessage) == null) renderMessages(false);
        View target = messages.findViewWithTag(revealedSearchMessage);
        if (target == null) return;
        current.getViewTreeObserver().addOnPreDrawListener(new ViewTreeObserver.OnPreDrawListener() {
            @Override public boolean onPreDraw() {
                current.getViewTreeObserver().removeOnPreDrawListener(this);
                current.scrollTo(0, target.getTop());
                return true;
            }
        });
        target.setAlpha(0.55f);
        target.animate().alpha(1f).setDuration(MOTION_OPEN).start();
    }

    private final class ChatSearch {
        private final Contact contact;
        private final FrameLayout origin;
        private final LinearLayout header;
        private final ImageButton anchor;
        private final FrameLayout body;
        private final LinearLayout conversation;
        private final PathInterpolator opening = new PathInterpolator(.22f, 1f, .36f, 1f);
        private final PathInterpolator closing = new PathInterpolator(.4f, 0f, .62f, 1f);
        private final OvershootInterpolator closePop = new OvershootInterpolator(.85f);
        private final ArgbEvaluator colors = new ArgbEvaluator();
        private FrameLayout bar;
        private FrameLayout layer;
        private View shade;
        private ScrollView panel;
        private LinearLayout results;
        private EditText query;
        private ImageButton cancel;
        private ImageButton submitButton;
        private ValueAnimator barMotion;
        private ValueAnimator panelMotion;
        private float extent;
        private float panelExtent;
        private boolean open;
        private String command = "";
        private String submittedKeyword = "";

        ChatSearch(Contact contact, FrameLayout origin, LinearLayout header, ImageButton anchor,
                   FrameLayout body, LinearLayout conversation) {
            this.contact = contact;
            this.origin = origin;
            this.header = header;
            this.anchor = anchor;
            this.body = body;
            this.conversation = conversation;
        }

        private void createViews() {
            bar = new FrameLayout(MainActivity.this) {
                private final RectF bounds = new RectF();
                private final android.graphics.Path outline = new android.graphics.Path();
                private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);

                private void updateBounds() {
                    bounds.set(anchor.getLeft() * (1 - extent), anchor.getTop() * (1 - extent),
                            anchor.getRight() + (getWidth() - anchor.getRight()) * extent,
                            anchor.getBottom() + (getHeight() - anchor.getBottom()) * extent);
                }

                @Override protected void dispatchDraw(Canvas canvas) {
                    updateBounds();
                    float radius = dp(14) + dp(6) * extent;
                    outline.rewind();
                    outline.addRoundRect(bounds, radius, radius, android.graphics.Path.Direction.CW);
                    paint.setStyle(Paint.Style.FILL);
                    paint.setColor((int) colors.evaluate(extent, TINT, WHITE));
                    canvas.drawPath(outline, paint);
                    int save = canvas.save();
                    canvas.clipPath(outline);
                    super.dispatchDraw(canvas);
                    canvas.restoreToCount(save);
                    paint.setStyle(Paint.Style.STROKE);
                    paint.setStrokeWidth(dp(1));
                    paint.setColor(OUTLINE);
                    paint.setAlpha(Math.round(255 * extent));
                    canvas.drawRoundRect(bounds, radius, radius, paint);
                }

                @Override public boolean dispatchTouchEvent(MotionEvent event) {
                    if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
                        updateBounds();
                        if (!bounds.contains(event.getX(), event.getY())) return false;
                    }
                    return super.dispatchTouchEvent(event);
                }
            };
            LinearLayout controls = new LinearLayout(MainActivity.this);
            controls.setGravity(Gravity.CENTER_VERTICAL);
            controls.setPadding(dp(7), dp(5), dp(7), dp(5));
            submitButton = iconButton("搜索", R.drawable.ic_search, false, () -> {
                if (open) submit();
                else setOpen(true);
            });
            submitButton.setTranslationZ(dp(1));
            controls.addView(submitButton);
            query = field("搜索聊天记录", InputType.TYPE_CLASS_TEXT);
            query.setSingleLine(true);
            query.setImeOptions(EditorInfo.IME_ACTION_SEARCH);
            query.setBackgroundColor(Color.TRANSPARENT);
            controls.addView(query, new LinearLayout.LayoutParams(0, -2, 1));
            cancel = iconButton("关闭搜索", R.drawable.ic_close, false, () -> setOpen(false));
            cancel.setBackground(new InsetDrawable(ripple(surface(TINT, 18), false), dp(6)));
            controls.addView(cancel);
            controls.addOnLayoutChangeListener((view, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) ->
                    positionSearchButton());
            query.setOnEditorActionListener((view, action, event) -> {
                if (action != EditorInfo.IME_ACTION_SEARCH && (event == null
                        || event.getKeyCode() != KeyEvent.KEYCODE_ENTER || event.getAction() != KeyEvent.ACTION_DOWN)) {
                    return false;
                }
                submit();
                return true;
            });
            bar.addView(controls, new FrameLayout.LayoutParams(-1, -1));
            origin.addView(bar, new FrameLayout.LayoutParams(-1, -1));

            layer = new FrameLayout(MainActivity.this);
            shade = new View(MainActivity.this);
            shade.setBackgroundColor(0x99000000);
            shade.setContentDescription("关闭搜索");
            shade.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
            shade.setOnClickListener(view -> setOpen(false));
            layer.addView(shade, new FrameLayout.LayoutParams(-1, -1));
            panel = new ScrollView(MainActivity.this);
            panel.setBackground(card(WHITE, 20));
            panel.setClipToOutline(true);
            panel.setElevation(dp(5));
            panel.setClickable(true);
            panel.setVerticalScrollBarEnabled(false);
            panel.setVisibility(View.GONE);
            results = column();
            results.setPadding(dp(10), dp(10), dp(10), dp(10));
            panel.addView(results, new ScrollView.LayoutParams(-1, -2));
            FrameLayout.LayoutParams placement = new FrameLayout.LayoutParams(-1, -2, Gravity.TOP);
            placement.topMargin = dp(8);
            layer.addView(panel, placement);
            body.addView(layer, new FrameLayout.LayoutParams(-1, -1));
        }

        void setOpen(boolean value) {
            if (open == value) return;
            open = value;
            if (value && composerPanels != null) composerPanels.hide();
            if (bar == null) createViews();
            if (value) {
                anchor.setAlpha(0f);
                header.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS);
                conversation.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS);
                bar.setVisibility(View.VISIBLE);
                layer.setVisibility(View.VISIBLE);
            } else {
                command = "";
                hideKeyboard();
                header.setVisibility(View.VISIBLE);
            }
            ValueAnimator previous = barMotion;
            barMotion = null;
            if (previous != null) previous.cancel();
            float target = value ? 1f : 0f;
            ValueAnimator move = ValueAnimator.ofFloat(extent, target);
            barMotion = move;
            move.setDuration(Math.max(70, Math.round((value ? 300 : 220) * Math.abs(target - extent))));
            move.setInterpolator(value ? opening : closing);
            move.addUpdateListener(animation -> {
                extent = (float) animation.getAnimatedValue();
                applyMotion();
            });
            move.addListener(new AnimatorListenerAdapter() {
                @Override public void onAnimationEnd(Animator animation) {
                    if (barMotion != animation) return;
                    barMotion = null;
                    if (open && bar.isAttachedToWindow()) {
                        header.setVisibility(View.INVISIBLE);
                        query.requestFocus();
                        getSystemService(InputMethodManager.class).showSoftInput(query, InputMethodManager.SHOW_IMPLICIT);
                    } else if (!open) {
                        restoreHeader();
                    }
                }
            });
            applyMotion();
            move.start();
        }

        private void applyMotion() {
            positionSearchButton();
            float cancelProgress = Math.max(0, (extent - .59f) / .41f);
            cancel.setVisibility(cancelProgress > 0f ? View.VISIBLE : View.INVISIBLE);
            cancel.setAlpha(Math.min(1f, cancelProgress * 1.5f));
            float cancelScale = .64f + .36f * closePop.getInterpolation(cancelProgress);
            cancel.setScaleX(cancelScale);
            cancel.setScaleY(cancelScale);
            float queryProgress = Math.max(0, (extent - .74f) / .26f);
            query.setVisibility(queryProgress > 0f ? View.VISIBLE : View.INVISIBLE);
            query.setAlpha(queryProgress);
            shade.setAlpha(.19f * extent);
            float revealed = Math.min(extent, panelExtent);
            panel.setAlpha(revealed);
            panel.setTranslationY(-dp(7) * (1 - revealed));
            bar.invalidate();
        }

        private void positionSearchButton() {
            submitButton.setTranslationX((anchor.getLeft() - submitButton.getLeft()) * (1 - extent));
        }

        private void hideKeyboard() {
            getSystemService(InputMethodManager.class).hideSoftInputFromWindow(query.getWindowToken(), 0);
            query.clearFocus();
        }

        private void restoreHeader() {
            header.setVisibility(View.VISIBLE);
            anchor.setAlpha(1f);
            header.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_AUTO);
            conversation.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_AUTO);
            bar.setVisibility(View.GONE);
            layer.setVisibility(View.GONE);
            TransitionManager.endTransitions(layer);
            if (panelMotion != null) panelMotion.cancel();
            panelMotion = null;
            panelExtent = 0;
            panel.setVisibility(View.GONE);
            query.setText("");
            query.setError(null);
            results.removeAllViews();
        }

        void dispose() {
            if (bar == null) return;
            ValueAnimator previous = barMotion;
            barMotion = null;
            if (previous != null) previous.cancel();
            if (query.hasFocus()) hideKeyboard();
            restoreHeader();
        }

        private void resetResults() {
            if (panel.getVisibility() != View.VISIBLE) {
                panel.setVisibility(View.VISIBLE);
                panelMotion = ValueAnimator.ofFloat(0f, 1f);
                panelMotion.setDuration(MOTION_OPEN);
                panelMotion.setInterpolator(opening);
                panelMotion.addUpdateListener(animation -> {
                    panelExtent = (float) animation.getAnimatedValue();
                    applyMotion();
                });
                panelMotion.start();
            } else {
                ChangeBounds resize = new ChangeBounds();
                resize.addTarget(panel);
                resize.setDuration(MOTION_SURFACE);
                resize.setInterpolator(opening);
                TransitionManager.beginDelayedTransition(layer, resize);
            }
            results.removeAllViews();
            panel.scrollTo(0, 0);
        }

        private void status(String text, int color) {
            resetResults();
            addSearchStatus(text, color);
        }

        private void addSearchStatus(String text, int color) {
            TextView status = label(text, 14, color);
            status.setPadding(dp(10), dp(10), dp(10), dp(10));
            status.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
            results.addView(status);
        }

        private void submit() {
            String keyword = query.getText().toString().trim();
            if (keyword.isEmpty() || connection == null) {
                query.setError(keyword.isEmpty() ? "请输入关键词" : "连接尚未恢复");
                return;
            }
            hideKeyboard();
            command = UUID.randomUUID().toString();
            submittedKeyword = keyword;
            status("正在搜索…", MUTED);
            send(contact.group() ? "group_history" : "history_request", "private", !contact.group(),
                    contact.group() ? "group_id" : "target_user_code", contact.code(), "limit", HISTORY_PAGE,
                    "search_query", keyword, "command_id", command);
        }

        boolean error(String received, String detail) {
            if (command.isEmpty() || !command.equals(received)) return false;
            command = "";
            status(detail, DANGER);
            return true;
        }

        void connectionLost() {
            error(command, "连接已断开，请重新搜索");
        }

        void receive(JSONObject packet) throws JSONException {
            if (command.isEmpty() || !command.equals(packet.optString("command_id"))
                    || !isActive(packet.getString(contact.group() ? "group_id" : "target_user_code"))) return;
            command = "";
            JSONArray matches = packet.optJSONArray("messages");
            if (matches == null || matches.length() == 0) {
                status("未找到匹配消息", MUTED);
                return;
            }
            resetResults();
            for (int index = matches.length() - 1; index >= 0; index--) {
                ChatMessage row = message(matches.getJSONObject(index));
                String body = messageText(row);
                int match = body.indexOf(submittedKeyword);
                if (row.recalled() || match < 0) continue;
                LinearLayout result = column();
                result.setPadding(dp(12), dp(9), dp(12), dp(9));
                result.setMinimumHeight(dp(48));
                result.setFocusable(true);
                result.setBackground(ripple(surface(TINT, 10), false));
                TextView excerpt = label("", 15, INK);
                excerpt.setText(searchExcerpt(body, match));
                excerpt.setMaxLines(2);
                excerpt.setEllipsize(TextUtils.TruncateAt.END);
                result.addView(excerpt, new LinearLayout.LayoutParams(-1, -2));
                String time = messageTime(row, false);
                if (!time.isEmpty()) addSpaced(result, label(time, 11, MUTED), 3);
                result.setOnClickListener(view -> {
                    if (!open) return;
                    setOpen(false);
                    revealSearchMatch(contact, row);
                });
                addSpaced(results, result, results.getChildCount() == 0 ? 0 : 5);
            }
            if (results.getChildCount() == 0) addSearchStatus("未找到匹配消息", MUTED);
        }

        private CharSequence searchExcerpt(String body, int match) {
            int start = Math.max(0, match - 14);
            if (start > 0 && Character.isLowSurrogate(body.charAt(start))) start--;
            int matchEnd = match + submittedKeyword.length();
            int end = Math.min(body.length(), matchEnd + 58);
            if (end < body.length() && Character.isLowSurrogate(body.charAt(end))) end++;
            String lead = start == 0 ? "" : "…";
            String trail = end == body.length() ? "" : "…";
            SpannableStringBuilder excerpt = new SpannableStringBuilder(
                    lead + body.substring(start, end).replace('\n', ' ').replace('\r', ' ') + trail);
            int first = lead.length() + match - start;
            int last = first + submittedKeyword.length();
            excerpt.setSpan(new ForegroundColorSpan(ACCENT), first, last, 0);
            excerpt.setSpan(new StyleSpan(Typeface.BOLD), first, last, 0);
            return excerpt;
        }
    }

    private String accountKey(String type, String value) {
        return type + ":" + key(ownCode) + ":" + value;
    }

    private void restorePendingTexts() {
        String prefix = accountKey("outbox", "");
        for (Map.Entry<String, ?> entry : preferences().getAll().entrySet()) {
            if (!entry.getKey().startsWith(prefix)) continue;
            String id = entry.getKey().substring(prefix.length());
            try {
                if (!(entry.getValue() instanceof String json)) throw new JSONException("记录格式无效");
                JSONObject stored = new JSONObject(json);
                Contact peer = new Contact(stored.getString("peer"), stored.getString("name"), stored.optBoolean("group"));
                String text = stored.getString("text");
                Instant time = Instant.parse(stored.getString("time"));
                pendingTexts.put(id, new PendingText(peer, text, time));
                if (!peer.group()) contacts.putIfAbsent(key(peer.code()), peer);
                addMessage(peer.code(), new ChatMessage(id, ownCode, username, text, 0, time, "failed", 0, false, null), false);
            } catch (JSONException | DateTimeParseException error) {
                toast("无法恢复待发送消息 " + id + " · " + error.getMessage());
            }
        }
    }

    private void sendPendingText(String id) {
        PendingText pending = pendingTexts.get(id);
        if (pending == null) return;
        ChatConnection session = connection;
        if (!foreground || session == null) {
            failText(id);
            showConnectionStatus("连接尚未恢复");
            reconnect.run();
            return;
        }
        updatePendingStatus(id, pending, "sending");
        writers.execute(() -> {
            try {
                boolean group = pending.peer().group();
                session.send(packet(group ? "group_send" : "private_chat", group ? "group_id" : "target_user_code", pending.peer().code(),
                        "content", pending.text(), "message_id", id));
            } catch (IOException | JSONException error) {
                main.post(() -> {
                    if (connection != session || !pendingTexts.containsKey(id)) return;
                    failText(id);
                    toast("消息发送失败 · " + error.getMessage());
                    if (error instanceof IOException) {
                        disconnect();
                        reconnect.run();
                    }
                });
            }
        });
    }

    private void confirmText(String id) {
        if (pendingTexts.remove(id) != null) preferences().edit().remove(accountKey("outbox", id)).apply();
    }

    private void failText(String id) {
        PendingText pending = pendingTexts.get(id);
        if (pending != null) updatePendingStatus(id, pending, "failed");
    }

    private void updatePendingStatus(String id, PendingText pending, String state) {
        Map<String, ChatMessage> history = histories.get(key(pending.peer().code()));
        String identifier = key(ownCode) + ":" + id;
        ChatMessage row = history.get(identifier).withDelivery(state);
        history.put(identifier, row);
        TextView time = visibleMessageTimes.get(identifier);
        if (time != null) setMessageStatus(time, row, true);
    }

    private void requestHistory(Contact contact, String before) {
        String peer = key(contact.code());
        if (connection == null || pendingHistory.putIfAbsent(peer, before) != null) return;
        send(contact.group() ? "group_history" : "history_request", "private", !contact.group(),
                contact.group() ? "group_id" : "target_user_code", contact.code(), "limit", HISTORY_PAGE,
                "before_message_id", before, "command_id", "history:" + peer);
    }

    private void followConversation(boolean smooth, int previousY, boolean atEnd, int previousHeight) {
        ConversationScroll current = scroll;
        if (atEnd) {
            current.followArrivals(smooth);
            return;
        }
        LinearLayout content = messages;
        current.getViewTreeObserver().addOnPreDrawListener(new ViewTreeObserver.OnPreDrawListener() {
            @Override public boolean onPreDraw() {
                current.getViewTreeObserver().removeOnPreDrawListener(this);
                if (current != scroll || active == null || current.following) return true;
                int bottom = Math.max(0, content.getHeight() + current.getPaddingTop()
                        + current.getPaddingBottom() - current.getHeight());
                int offset = previousHeight > 0 ? content.getHeight() - previousHeight : 0;
                int next = Math.max(0, Math.min(previousY + offset, bottom));
                if (smooth && smoothScrollEnabled()) current.smoothScrollTo(0, next);
                else current.scrollTo(0, next);
                return true;
            }
        });
    }

    private void renderMessages(boolean jumpToEnd) {
        renderMessages(jumpToEnd, false);
    }

    private void renderMessages(boolean jumpToEnd, boolean prepend) {
        boolean atEnd = jumpToEnd || scroll.following;
        int previousY = scroll.getScrollY();
        int previousHeight = prepend ? messages.getHeight() : 0;
        messages.removeAllViews();
        visibleImages.clear();
        missingImages.clear();
        visibleMessageTimes.clear();
        renderedDate = null;
        Map<String, ChatMessage> history = histories.get(key(active.code()));
        if (history != null && !history.isEmpty()) {
            List<ChatMessage> rows = new ArrayList<>(history.values());
            rows.sort(MESSAGE_ORDER);
            int first = Math.max(0, rows.size() - visibleHistoryLimit);
            String cursor = historyCursor.get(key(active.code()));
            if (first > 0 || cursor != null) {
                Contact peer = active;
                addSpaced(messages, button("加载更早的消息", false, () -> {
                    scroll.pauseFollowing();
                    if (first > 0) {
                        visibleHistoryLimit = Math.min(rows.size(), visibleHistoryLimit + HISTORY_PAGE);
                        renderMessages(false, true);
                    } else {
                        requestHistory(peer, cursor);
                    }
                }), 4);
            }
            ChatMessage revealed = history.get(revealedSearchMessage);
            if (revealed != null && first > 0 && MESSAGE_ORDER.compare(revealed, rows.get(first)) < 0) {
                appendMessage(revealed, false);
            }
            for (int i = first; i < rows.size(); i++) appendMessage(rows.get(i), false);
        }
        followConversation(false, previousY, atEnd, previousHeight);
    }

    private final class ConversationScroll extends ScrollView {
        private final ValueAnimator movement = new ValueAnimator();
        private ImageButton latestButton;
        private boolean following = true;
        private boolean followQueued;
        private boolean animateNext;
        private boolean fingerDown;
        private boolean userScrolled;
        private int touchStartY;

        ConversationScroll() {
            super(MainActivity.this);
            movement.setDuration(MOTION_OPEN);
            movement.setInterpolator(ARRIVE);
            movement.addUpdateListener(animator -> scrollTo(0, (Integer) animator.getAnimatedValue()));
        }

        @Override public boolean dispatchTouchEvent(MotionEvent event) {
            int action = event.getActionMasked();
            if (action == MotionEvent.ACTION_DOWN) {
                fingerDown = true;
                touchStartY = getScrollY();
                pauseFollowing();
            }
            boolean handled = super.dispatchTouchEvent(event);
            if (action == MotionEvent.ACTION_MOVE && getScrollY() != touchStartY) userScrolled = true;
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
                fingerDown = false;
                userScrolled &= action == MotionEvent.ACTION_UP;
                if (userScrolled && !canScrollVertically(1)) jumpToLatest();
            }
            return handled;
        }

        @Override protected void onLayout(boolean changed, int left, int top, int right, int bottom) {
            super.onLayout(changed, left, top, right, bottom);
            updateLatestButton();
        }

        @Override protected void onScrollChanged(int x, int y, int oldX, int oldY) {
            super.onScrollChanged(x, y, oldX, oldY);
            if (!fingerDown && userScrolled && y > oldY && !canScrollVertically(1)) jumpToLatest();
            updateLatestButton();
        }

        @Override protected void onDetachedFromWindow() {
            movement.cancel();
            super.onDetachedFromWindow();
        }

        void pauseFollowing() {
            following = false;
            userScrolled = false;
            movement.cancel();
            updateLatestButton();
        }

        void jumpToLatest() {
            following = true;
            userScrolled = false;
            updateLatestButton();
            followArrivals(true);
        }

        private void updateLatestButton() {
            if (latestButton != null) {
                latestButton.setVisibility(!following && canScrollVertically(1) ? View.VISIBLE : View.GONE);
            }
        }

        void followArrivals(boolean smooth) {
            if (!following) return;
            if (smooth && !smoothScrollEnabled()) smooth = false;
            animateNext = smooth;
            if (followQueued) return;
            followQueued = true;
            getViewTreeObserver().addOnPreDrawListener(new ViewTreeObserver.OnPreDrawListener() {
                @Override public boolean onPreDraw() {
                    getViewTreeObserver().removeOnPreDrawListener(this);
                    followQueued = false;
                    if (scroll != ConversationScroll.this || active == null || !following) return true;
                    int bottom = Math.max(0, getChildAt(0).getHeight() + getPaddingTop()
                            + getPaddingBottom() - getHeight());
                    movement.cancel();
                    if (animateNext && getScrollY() != bottom) {
                        movement.setIntValues(getScrollY(), bottom);
                        movement.start();
                    } else {
                        scrollTo(0, bottom);
                    }
                    return true;
                }
            });
            invalidate();
        }
    }

    private void appendRecalledNotice(ChatMessage row, boolean self) {
        TextView notice = label(recalledNotice(row, self), 12, MUTED);
        notice.setGravity(Gravity.CENTER);
        LinearLayout line = new LinearLayout(this);
        line.setTag(messageKey(row));
        line.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams position = new LinearLayout.LayoutParams(-1, -2);
        position.topMargin = dp(9);
        messages.addView(line, position);
        line.addView(notice, new LinearLayout.LayoutParams(-2, -2));
    }

    private String recalledNotice(ChatMessage row, boolean self) {
        if (self || active == null || !active.group()) return RECALLED_TEXT;
        return row.authorName() + " 撤回了一条消息";
    }

    private void appendMessage(ChatMessage row, boolean animate) {
        if (row.time() != null) {
            LocalDate date = row.time().atZone(ZoneId.systemDefault()).toLocalDate();
            if (!date.equals(renderedDate)) {
                renderedDate = date;
                TextView separator = label(date.equals(LocalDate.now()) ? "今天" : date.format(DATE), 12, MUTED);
                separator.setGravity(Gravity.CENTER);
                addSpaced(messages, separator, 15);
            }
        }
        boolean self = ownCode.equalsIgnoreCase(row.author());
        if (row.recalled()) {
            appendRecalledNotice(row, self);
            return;
        }
        LinearLayout line = animate ? new MessageArrival(this, self) : new LinearLayout(this);
        line.setTag(messageKey(row));
        line.setGravity(self ? Gravity.END : Gravity.START);
        line.setClipChildren(false);
        line.setClipToPadding(false);
        line.setPadding(dp(2), 0, dp(2), 0);
        LinearLayout.LayoutParams linePosition = new LinearLayout.LayoutParams(-1, -2);
        linePosition.topMargin = dp(9);
        messages.addView(line, linePosition);

        LinearLayout cluster = column();
        cluster.setGravity(self ? Gravity.END : Gravity.START);
        line.addView(cluster, new LinearLayout.LayoutParams(-2, -2));
        if (active.group() && !self) addSpaced(cluster, label(row.authorName(), 12, MUTED), 0);
        int availableWidth = messages.getWidth();
        if (availableWidth == 0) availableWidth = getResources().getDisplayMetrics().widthPixels - dp(36);
        int bubbleWidth = Math.min(dp(420), Math.round(availableWidth * 0.82f));

        if (row.imageSize() > 0) {
            int imageWidth = Math.min(dp(184), bubbleWidth);
            cluster.addView(imageTile(active, row), new LinearLayout.LayoutParams(imageWidth, imageWidth * 3 / 4));
        } else if (row.attachment() != null) {
            ChatConnection.Attachment file = row.attachment();
            Contact conversation = active;
            Button save = button(file.name() + "\n保存 · " + android.text.format.Formatter.formatFileSize(this, file.size()),
                    false, () -> downloadFile(conversation, file));
            save.setMaxWidth(bubbleWidth);
            save.setMaxLines(3);
            save.setEllipsize(TextUtils.TruncateAt.END);
            save.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
            cluster.addView(save, new LinearLayout.LayoutParams(-2, -2));
        } else {
            TextView bubble = label("", 16, self ? BUBBLE_SELF_INK : INK);
            String body = messageText(row);
            int quoteEnd = body.startsWith("> ") ? body.indexOf('\n') : -1;
            if (quoteEnd < 0) {
                bubble.setText(body);
            } else {
                SpannableStringBuilder formatted = new SpannableStringBuilder(body);
                formatted.setSpan(new StyleSpan(Typeface.ITALIC), 0, quoteEnd, 0);
                bubble.setText(formatted);
            }
            bubble.setHorizontallyScrolling(false);
            bubble.setBreakStrategy(Layout.BREAK_STRATEGY_SIMPLE);
            bubble.setHyphenationFrequency(Layout.HYPHENATION_FREQUENCY_NONE);
            bubble.setMaxWidth(bubbleWidth);
            bubble.setLineSpacing(dp(3), 1f);
            bubble.setPadding(dp(14), dp(10), dp(14), dp(10));
            GradientDrawable shape = self ? surface(BUBBLE_SELF, 18) : card(WHITE, 18);
            float outer = dp(18);
            float start = self ? outer : dp(5);
            float end = self ? dp(5) : outer;
            shape.setCornerRadii(new float[]{outer, outer, outer, outer, end, end, start, start});
            bubble.setBackground(shape);
            if (!row.recalled()) {
                Contact peer = active;
                bubble.setOnLongClickListener(anchor -> {
                    showMessageActions(anchor, peer, row, self);
                    return true;
                });
            }
            cluster.addView(bubble, new LinearLayout.LayoutParams(-2, -2));
        }
        String timestamp = messageTime(row, self);
        if (!timestamp.isEmpty()) {
            TextView time = label(timestamp, 11, MUTED);
            setMessageStatus(time, row, self);
            LinearLayout.LayoutParams timePosition = new LinearLayout.LayoutParams(-2, -2);
            timePosition.topMargin = dp(3);
            timePosition.setMarginStart(dp(2));
            timePosition.setMarginEnd(dp(2));
            cluster.addView(time, timePosition);
            if (self) visibleMessageTimes.put(messageKey(row), time);
        }
        if (line instanceof MessageArrival arrival) arrival.play(cluster);
    }

    private String messageTime(ChatMessage row, boolean includeDelivery) {
        String clock = row.time() == null ? "" : row.time().atZone(ZoneId.systemDefault()).format(CLOCK);
        if (row.recalled()) return clock;
        if (row.revision() > 0) clock += (clock.isEmpty() ? "" : " · ") + "已编辑";
        if (includeDelivery && "delivered".equals(row.delivery())) clock += (clock.isEmpty() ? "" : " · ") + "已送达";
        if (includeDelivery && "sending".equals(row.delivery())) clock += " · 发送中";
        if (includeDelivery && "failed".equals(row.delivery())) clock += " · 未发送，点此重试";
        return clock;
    }

    private void setMessageStatus(TextView time, ChatMessage row, boolean self) {
        boolean retry = self && "failed".equals(row.delivery());
        time.setText(messageTime(row, self));
        time.setTextColor(retry ? DANGER : MUTED);
        time.setOnClickListener(retry ? view -> sendPendingText(row.id()) : null);
        time.setClickable(retry);
    }

    private static String messageText(ChatMessage row) {
        if (!"BOT001".equalsIgnoreCase(row.author())) return row.text();
        String plain = BOT_BOLD.matcher(row.text()).replaceAll("$1");
        plain = BOT_BULLET.matcher(plain).replaceAll("• ");
        return BOT_EMPTY_LINES.matcher(plain).replaceAll("\n\n").trim();
    }

    private PopupWindow chatPopup(View content, int width) {
        PopupWindow popup = new PopupWindow(content, dp(width), -2, true);
        popup.setBackgroundDrawable(card(WHITE, 16));
        popup.setElevation(dp(9));
        popup.setOutsideTouchable(true);
        return popup;
    }

    private void showChatPopup(PopupWindow popup, View anchor, int gravity) {
        View content = popup.getContentView();
        content.measure(View.MeasureSpec.makeMeasureSpec(popup.getWidth(), View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
        Rect frame = new Rect();
        anchor.getWindowVisibleDisplayFrame(frame);
        int[] location = new int[2];
        anchor.getLocationOnScreen(location);
        int below = frame.bottom - location[1] - anchor.getHeight();
        int height = content.getMeasuredHeight() + dp(4);
        int offset = below < height ? -anchor.getHeight() - height : dp(4);
        popup.showAsDropDown(anchor, 0, offset, gravity);
    }

    private void showMessageActions(View anchor, Contact peer, ChatMessage row, boolean self) {
        LinearLayout actions = column();
        actions.setPadding(dp(5), dp(5), dp(5), dp(5));
        PopupWindow popup = chatPopup(actions, 136);
        actions.addView(button("引用回复", false, () -> {
            selectedQuote = row;
            quotePreview.setText(quoteLine(peer, row));
            quoteBar.setVisibility(View.VISIBLE);
            popup.dismiss();
            EditText input = draftInput;
            input.post(() -> {
                if (input != draftInput || !isActive(peer.code())) return;
                input.requestFocus();
                getSystemService(InputMethodManager.class).showSoftInput(input, InputMethodManager.SHOW_IMPLICIT);
            });
        }), new LinearLayout.LayoutParams(-1, -2));
        actions.addView(button("复制", false, () -> {
            getSystemService(ClipboardManager.class).setPrimaryClip(ClipData.newPlainText("消息", row.text()));
            popup.dismiss();
        }), new LinearLayout.LayoutParams(-1, -2));
        if (self && !pendingTexts.containsKey(row.id())) {
            addSpaced(actions, button("撤回", false, () -> {
                popup.dismiss();
                recallMessage(row);
            }), 4);
        }
        if (self && !peer.group() && editingAvailable && !pendingTexts.containsKey(row.id())) {
            addSpaced(actions, button("编辑", false, () -> {
                popup.dismiss();
                editMessage(peer, row);
            }), 4);
        }
        showChatPopup(popup, anchor, self ? Gravity.END : Gravity.START);
    }

    private String quoteLine(Contact peer, ChatMessage row) {
        String author = ownCode.equalsIgnoreCase(row.author()) ? username : (peer.group() ? row.authorName() : peer.name());
        author = author.replaceAll("[:：\\r\\n]", " ").trim();
        if (author.length() > 40) author = author.substring(0, 40);
        String text = messageText(row);
        int quoteEnd = text.startsWith("> ") ? text.indexOf('\n') : -1;
        if (quoteEnd >= 0) text = text.substring(quoteEnd + 1);
        text = text.replaceAll("\\s+", " ").trim();
        if (text.length() > 120) text = text.substring(0, 120) + "…";
        return "> " + author + ": " + text;
    }

    private void editMessage(Contact peer, ChatMessage row) {
        LinearLayout body = column();
        body.setPadding(dp(20), dp(10), dp(20), 0);
        EditText editor = field("输入消息", InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE);
        editor.setText(row.text());
        editor.setSelection(editor.length());
        editor.setMaxLines(6);
        body.addView(editor);
        AlertDialog dialog = new AlertDialog.Builder(this).setTitle("编辑消息").setView(body)
                .setNegativeButton("取消", null).setPositiveButton("保存", null).create();
        dialog.setOnShowListener(ignored -> {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view -> {
                String text = editor.getText().toString();
                if (text.trim().isEmpty()) {
                    editor.setError("请输入消息");
                    return;
                }
                if (!text.equals(row.text())) {
                    if (connection == null) {
                        editor.setError("连接尚未恢复，请稍后重试");
                        return;
                    }
                    send("dm_edit", "target_user_code", peer.code(), "message_id", row.id(), "revision", row.revision(),
                            "content", text, "command_id", UUID.randomUUID().toString());
                }
                dialog.dismiss();
            });
            editor.requestFocus();
        });
        dialog.getWindow().setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
        dialog.show();
    }

    private void recallMessage(ChatMessage row) {
        if (connection == null) {
            toast("连接尚未恢复，请稍后重试");
            return;
        }
        send("message_recall", "message_id", row.id(), "command_id", UUID.randomUUID().toString());
    }

    private enum ComposerTool { EMOJI, ATTACHMENT }

    private final class ComposerPanels extends FrameLayout {
        private final Contact contact;
        private final LinearLayout composer;
        private final EditText input;
        private final ImageButton attachmentButton;
        private final ImageButton emojiButton;
        private final ScrollView panel;
        private final LinearLayout contents;
        private ComposerTool selected;
        private ComposerTool displayed;
        private ValueAnimator motion;
        private float extent;

        ComposerPanels(Contact contact, LinearLayout composer, EditText input,
                       ImageButton attachmentButton, ImageButton emojiButton) {
            super(MainActivity.this);
            this.contact = contact;
            this.composer = composer;
            this.input = input;
            this.attachmentButton = attachmentButton;
            this.emojiButton = emojiButton;
            setVisibility(View.GONE);
            setClipChildren(false);
            panel = new ScrollView(MainActivity.this) {
                @Override protected void onMeasure(int width, int height) {
                    int available = Math.max(0, MeasureSpec.getSize(height) - composer.getMeasuredHeight() - dp(12));
                    super.onMeasure(width, MeasureSpec.makeMeasureSpec(available, MeasureSpec.AT_MOST));
                }
            };
            panel.setBackground(card(WHITE, 21));
            panel.setClipToOutline(true);
            panel.setElevation(dp(5));
            panel.setVerticalScrollBarEnabled(false);
            panel.setFocusable(false);
            panel.setClickable(true);
            contents = column();
            contents.setPadding(dp(8), dp(8), dp(8), dp(8));
            panel.addView(contents, new ScrollView.LayoutParams(-1, -2));
            addView(panel, new FrameLayout.LayoutParams(-1, -2, Gravity.BOTTOM));
        }

        void toggle(ComposerTool tool) {
            if (selected == tool) {
                hide();
                return;
            }
            selected = tool;
            if (displayed != tool || tool == ComposerTool.ATTACHMENT) {
                if (extent == 1f && isLaidOut()) {
                    ChangeBounds resize = new ChangeBounds();
                    resize.addTarget(panel);
                    resize.setDuration(MOTION_SURFACE);
                    resize.setInterpolator(ARRIVE);
                    TransitionManager.beginDelayedTransition(this, resize);
                }
                displayed = tool;
                contents.removeAllViews();
                if (tool == ComposerTool.EMOJI) addEmojiGrid();
                else addAttachmentOptions();
                panel.scrollTo(0, 0);
            }
            setVisibility(View.VISIBLE);
            animatePanel(1f);
        }

        boolean hide() {
            if (selected == null) return false;
            selected = null;
            animatePanel(0f);
            return true;
        }

        void dispose() {
            ValueAnimator previous = motion;
            motion = null;
            if (previous != null) previous.cancel();
            TransitionManager.endTransitions(this);
            setVisibility(View.GONE);
        }

        private void animatePanel(float target) {
            ValueAnimator previous = motion;
            motion = null;
            if (previous != null) previous.cancel();
            attachmentButton.setBackground(selected == ComposerTool.ATTACHMENT
                    ? ripple(surface(TINT, 14), false) : controlBackground(false));
            emojiButton.setBackground(selected == ComposerTool.EMOJI
                    ? ripple(surface(TINT, 14), false) : controlBackground(false));
            if (extent == target) {
                if (target == 0f) setVisibility(View.GONE);
                return;
            }
            ValueAnimator next = ValueAnimator.ofFloat(extent, target);
            motion = next;
            next.setDuration(Math.max(70, Math.round((target == 1f ? 240 : 180) * Math.abs(target - extent))));
            next.setInterpolator(ARRIVE);
            next.addUpdateListener(animation -> {
                extent = (float) animation.getAnimatedValue();
                positionPanel();
            });
            next.addListener(new AnimatorListenerAdapter() {
                @Override public void onAnimationEnd(Animator animation) {
                    if (motion != animation) return;
                    motion = null;
                    if (target == 0f) setVisibility(View.GONE);
                }
            });
            positionPanel();
            next.start();
        }

        @Override protected void onLayout(boolean changed, int left, int top, int right, int bottom) {
            super.onLayout(changed, left, top, right, bottom);
            positionPanel();
        }

        private void positionPanel() {
            View source = displayed == ComposerTool.EMOJI ? emojiButton : attachmentButton;
            panel.setPivotX(source.getLeft() + source.getWidth() * .5f);
            panel.setPivotY(panel.getHeight());
            panel.setTranslationY(composer.getTop() - getHeight() - dp(6) + dp(8) * (1 - extent));
            panel.setScaleX(.975f + .025f * extent);
            panel.setScaleY(.94f + .06f * extent);
            panel.setAlpha(extent);
        }

        @Override public boolean dispatchTouchEvent(MotionEvent event) {
            if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
                if (selected == null || event.getY() >= composer.getTop()) return false;
                if (event.getY() < panel.getY() || event.getY() > panel.getY() + panel.getHeight()) {
                    hide();
                    return true;
                }
            }
            return super.dispatchTouchEvent(event);
        }

        private void addEmojiGrid() {
            int columns = Math.min(6, Math.max(1, (chatBody.getWidth() - dp(16)) / dp(48)));
            for (int start = 0; start < EMOJI.length; start += columns) {
                LinearLayout line = new LinearLayout(MainActivity.this);
                line.setWeightSum(columns);
                for (int index = start; index < Math.min(start + columns, EMOJI.length); index++) {
                    String symbol = EMOJI[index];
                    TextView item = label(symbol, 23, INK);
                    item.setTypeface(Typeface.DEFAULT);
                    item.setGravity(Gravity.CENTER);
                    item.setBackground(ripple(surface(WHITE, 13), false));
                    item.setOnClickListener(view -> {
                        if (selected != ComposerTool.EMOJI) return;
                        int first = input.getSelectionStart();
                        int last = input.getSelectionEnd();
                        if (first < 0 || last < 0) first = last = input.length();
                        int startSelection = Math.min(first, last);
                        input.getText().replace(startSelection, Math.max(first, last), symbol);
                        input.setSelection(startSelection + symbol.length());
                    });
                    line.addView(item, new LinearLayout.LayoutParams(0, dp(48), 1));
                }
                contents.addView(line, new LinearLayout.LayoutParams(-1, -2));
            }
        }

        private void addAttachmentOptions() {
            LinearLayout row = new LinearLayout(MainActivity.this);
            if (imagesAvailable) addAttachmentOption(row, "图片", R.drawable.ic_photo, true);
            if (filesAvailable) addAttachmentOption(row, "文件", R.drawable.ic_document, false);
            contents.addView(row, new LinearLayout.LayoutParams(-1, -2));
        }

        private void addAttachmentOption(LinearLayout row, String name, int icon, boolean image) {
            LinearLayout option = column();
            option.setGravity(Gravity.CENTER);
            option.setBackground(ripple(surface(TINT, 13), false));
            option.setMinimumHeight(dp(74));
            option.setContentDescription(name);
            option.setFocusable(true);
            option.setOnClickListener(view -> {
                if (selected == ComposerTool.ATTACHMENT) launchAttachmentPicker(contact, image);
            });
            ImageView graphic = new ImageView(MainActivity.this);
            graphic.setImageResource(icon);
            graphic.setImageTintList(ColorStateList.valueOf(ACCENT));
            graphic.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
            option.addView(graphic, new LinearLayout.LayoutParams(dp(24), dp(24)));
            TextView title = label(name, 14, ACCENT);
            title.setTypeface(mediumFont);
            title.setGravity(Gravity.CENTER);
            title.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
            addSpaced(option, title, 5);
            LinearLayout.LayoutParams position = new LinearLayout.LayoutParams(0, -2, 1);
            if (row.getChildCount() > 0) position.setMarginStart(dp(8));
            row.addView(option, position);
        }
    }

    private void launchAttachmentPicker(Contact contact, boolean image) {
        if (!image && transferringFile) {
            toast("请等待当前文件传输完成");
            return;
        }
        composerPanels.hide();
        if (image) {
            pendingImagePeer = contact.code();
            pendingImageGroup = contact.group();
        } else {
            pendingFilePeer = contact.code();
            pendingFileGroup = contact.group();
        }
        Intent picker = new Intent(Intent.ACTION_OPEN_DOCUMENT).setType(image ? "image/*" : "*/*")
                .addCategory(Intent.CATEGORY_OPENABLE);
        startActivityForResult(picker, image ? IMAGE_PICK : FILE_PICK);
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        Uri selected = result == RESULT_OK && data != null ? data.getData() : null;
        if (request == SAVE_DOCUMENT) {
            SavedDocument document = pendingSave;
            pendingSave = null;
            if (document != null) {
                if (selected != null) writeDocument(document, selected);
                else if (document.temporary()) document.source().delete();
            }
            return;
        }
        if (request == FILE_PICK) {
            if (selected == null || pendingFilePeer == null) return;
            if (connection == null) pendingFileUri = selected;
            else sendFile(selected);
            return;
        }
        if (request != IMAGE_PICK || selected == null || pendingImagePeer == null) return;
        if (connection == null) {
            pendingImageUri = selected;
            return;
        }
        sendImage(selected);
    }

    /**
     * Transfers run in the background behind a small chip above the composer instead of a modal
     * dialog, so the conversation stays usable while a file is uploading or downloading.
     */
    private void beginFileTransfer(String title) {
        hideTransferChip();
        transferringFile = true;
        transferTitle = title;
        if (screen == null) return;
        LinearLayout chip = new LinearLayout(this);
        chip.setOrientation(LinearLayout.VERTICAL);
        chip.setPadding(dp(14), dp(10), dp(14), dp(10));
        chip.setBackground(card(PAPER, 14));
        chip.setElevation(dp(6));
        transferChipText = label(title + " · 0%", 13, INK);
        chip.addView(transferChipText, new LinearLayout.LayoutParams(-1, -2));
        transferBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        transferBar.setMax(100);
        transferBar.setProgressTintList(ColorStateList.valueOf(ACCENT));
        LinearLayout.LayoutParams bar = new LinearLayout.LayoutParams(-1, dp(4));
        bar.topMargin = dp(8);
        chip.addView(transferBar, bar);
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(-1, -2);
        params.gravity = Gravity.BOTTOM;
        params.setMargins(dp(16), 0, dp(16), dp(86));
        screen.addView(chip, params);
        transferChip = chip;
    }

    private void updateTransfer(long bytes, long size) {
        int percentage = size <= 0 ? 100 : (int) Math.min(100, bytes * 100 / size);
        main.post(() -> {
            if (isDestroyed() || transferBar == null) return;
            transferBar.setProgress(percentage);
            if (transferChipText != null) transferChipText.setText(transferTitle + " · " + percentage + "%");
        });
    }

    private void endFileTransfer() {
        transferringFile = false;
        hideTransferChip();
    }

    private void hideTransferChip() {
        if (transferChip != null && transferChip.getParent() == screen) screen.removeView(transferChip);
        transferChip = null;
        transferBar = null;
        transferChipText = null;
    }

    private void sendFile(Uri selected) {
        if (transferringFile) {
            toast("请等待当前文件传输完成");
            return;
        }
        boolean group = pendingFileGroup;
        if (!filesAvailable || (group && !groupAttachmentsAvailable)) {
            toast("服务器暂未开放普通文件传输");
            return;
        }
        String peer = pendingFilePeer;
        ChatConnection session = connection;
        beginFileTransfer("正在发送文件");
        fileTransfers.execute(() -> {
            File staging = null;
            String detail = null;
            try {
                String name;
                try (Cursor cursor = getContentResolver().query(selected, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
                    if (cursor == null || !cursor.moveToFirst()) throw new IOException("无法读取所选文件的名称");
                    name = cursor.getString(0);
                }
                if (!ChatConnection.Attachment.validName(name)) throw new IOException("文件名不符合发送要求");
                staging = File.createTempFile("upload-", ".part", getCacheDir());
                try (InputStream input = getContentResolver().openInputStream(selected);
                     FileOutputStream output = new FileOutputStream(staging)) {
                    if (input == null) throw new IOException("无法打开所选文件");
                    byte[] buffer = new byte[32 * 1024];
                    long total = 0;
                    for (int length; (length = input.read(buffer)) != -1; ) {
                        total += length;
                        if (total > ChatConnection.MAX_FILE_BYTES) throw new IOException("文件超过 32 MiB");
                        output.write(buffer, 0, length);
                    }
                }
                long size = staging.length();
                session.uploadAttachment(staging, name, peer, group, count -> updateTransfer(count, size));
            } catch (IOException | JSONException | SecurityException error) {
                detail = error.getMessage() == null ? error.getClass().getSimpleName() : error.getMessage();
            } finally {
                if (staging != null) staging.delete();
            }
            String result = detail;
            main.post(() -> {
                if (isDestroyed()) return;
                endFileTransfer();
                if (result != null) toast("文件发送失败 · " + result);
            });
        });
    }

    private void downloadFile(Contact conversation, ChatConnection.Attachment file) {
        ChatConnection session = connection;
        if (session == null || transferringFile) {
            toast(session == null ? "连接尚未恢复，请稍后重试" : "请等待当前文件传输完成");
            return;
        }
        Group membership = groupDirectory.get(key(conversation.code()));
        beginFileTransfer("正在下载文件");
        fileTransfers.execute(() -> {
            File staging = null;
            try {
                staging = File.createTempFile("download-", ".part", getCacheDir());
                session.downloadAttachment(file, staging, count -> updateTransfer(count, file.size()));
                SavedDocument document = new SavedDocument(staging, file.name(), "application/octet-stream", true);
                main.post(() -> {
                    endFileTransfer();
                    Group current = groupDirectory.get(key(conversation.code()));
                    if (conversation.group() && (membership == null || current == null
                            || membership.joinedAfter() != current.joinedAfter())) {
                        document.source().delete();
                        if (!isDestroyed()) toast("该群聊的成员资格已变更");
                        return;
                    }
                    if (isDestroyed() || ownCode == null || !foreground) {
                        document.source().delete();
                        return;
                    }
                    chooseSaveLocation(document);
                });
            } catch (IOException | JSONException error) {
                if (staging != null) staging.delete();
                main.post(() -> {
                    if (isDestroyed()) return;
                    endFileTransfer();
                    toast("文件下载失败 · " + error.getMessage());
                });
            }
        });
    }

    private void chooseSaveLocation(SavedDocument document) {
        pendingSave = document;
        startActivityForResult(new Intent(Intent.ACTION_CREATE_DOCUMENT).setType(document.mime())
                .addCategory(Intent.CATEGORY_OPENABLE).putExtra(Intent.EXTRA_TITLE, document.name()), SAVE_DOCUMENT);
    }

    private void writeDocument(SavedDocument document, Uri destination) {
        fileTransfers.execute(() -> {
            try (OutputStream output = getContentResolver().openOutputStream(destination, "w")) {
                if (output == null) throw new IOException("无法打开保存位置");
                Files.copy(document.source().toPath(), output);
                main.post(() -> toast("已保存 " + document.name()));
            } catch (IOException | SecurityException error) {
                main.post(() -> toast("保存失败 · " + error.getMessage()));
            } finally {
                if (document.temporary()) document.source().delete();
            }
        });
    }

    private void sendImage(Uri uri) {
        String peer = pendingImagePeer;
        boolean group = pendingImageGroup;
        if (!imagesAvailable || (group && !groupAttachmentsAvailable)) {
            toast("服务器暂未开放图片传输");
            return;
        }
        ChatConnection session = connection;
        if (transferringFile) {
            toast("请等待当前文件传输完成");
            return;
        }
        beginFileTransfer("正在发送图片");
        writers.execute(() -> {
            try {
                Bitmap picture = ImageDecoder.decodeBitmap(ImageDecoder.createSource(getContentResolver(), uri), (decoder, info, source) -> {
                    int width = info.getSize().getWidth();
                    int height = info.getSize().getHeight();
                    int longest = Math.max(width, height);
                    if (longest > 2048) decoder.setTargetSize(Math.max(1, width * 2048 / longest), Math.max(1, height * 2048 / longest));
                    decoder.setAllocator(ImageDecoder.ALLOCATOR_SOFTWARE);
                });
                ByteArrayOutputStream output = new ByteArrayOutputStream();
                if (!picture.compress(Bitmap.CompressFormat.JPEG, 88, output) || output.size() > MAX_IMAGE) {
                    throw new IOException("图片压缩后仍超过 4 MiB");
                }
                byte[] bytes = output.toByteArray();
                String id = UUID.randomUUID().toString();
                for (int index = 0, offset = 0; offset < bytes.length; index++) {
                    int length = TransportCodec.chunkLength(bytes.length, index);
                    session.send(packet("dm_image_chunk", "message_id", id, group ? "group_id" : "target_user_code", peer,
                            "image_size", bytes.length, "chunk_index", index,
                            "image_data", Base64.encodeToString(bytes, offset, length, Base64.NO_WRAP)));
                    offset += length;
                    updateTransfer(offset, bytes.length);
                }
            } catch (IOException | JSONException error) {
                main.post(() -> toast("图片发送失败 · " + error.getMessage()));
            } finally {
                main.post(() -> {
                    if (!isDestroyed()) endFileTransfer();
                });
            }
        });
    }

    private void fetchImage(Contact peer, ChatMessage row) {
        download = new ImageDownload(peer, row, UUID.randomUUID().toString(), new ByteArrayOutputStream(row.imageSize()));
        requestImageChunk();
    }

    private FrameLayout imageTile(Contact peer, ChatMessage row) {
        File file = cachedImage(peer, row);
        FrameLayout box = new FrameLayout(this);
        box.setBackground(surface(TINT, 17));
        box.setClipToOutline(true);
        ImageView thumbnail = new ImageView(this);
        thumbnail.setContentDescription("打开大图");
        thumbnail.setScaleType(ImageView.ScaleType.CENTER_CROP);
        thumbnail.setForeground(new RippleDrawable(ColorStateList.valueOf(RIPPLE_ACCENT), null, surface(WHITE, 17)));
        thumbnail.setOnClickListener(view -> showImage(file));
        thumbnail.setVisibility(View.INVISIBLE);
        box.addView(thumbnail, new FrameLayout.LayoutParams(-1, -1));
        TextView status = label("正在加载图片…", 14, MUTED);
        status.setGravity(Gravity.CENTER);
        box.addView(status, new FrameLayout.LayoutParams(-1, -1));
        visibleImages.put(file, new ImageTile(peer, row, thumbnail, status));

        Bitmap bitmap = thumbnails.get(file);
        if (bitmap != null) {
            showThumbnail(file, bitmap);
        } else if (queuedThumbnails.add(file)) {
            imageLoads.execute(() -> {
                boolean exists = file.isFile();
                Bitmap decoded = exists ? decodeThumbnail(file) : null;
                main.post(() -> {
                    queuedThumbnails.remove(file);
                    if (isDestroyed()) return;
                    if (exists) {
                        showThumbnail(file, decoded);
                    } else if (visibleImages.containsKey(file)) {
                        missingImages.add(file);
                        loadNextImage();
                    }
                });
            });
        }
        return box;
    }

    private static Bitmap decodeThumbnail(File file) {
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inSampleSize = 4;
        return BitmapFactory.decodeFile(file.getPath(), options);
    }

    private void showThumbnail(File file, Bitmap bitmap) {
        ImageTile tile = visibleImages.get(file);
        missingImages.remove(file);
        if (bitmap == null) {
            if (tile != null) tile.status().setText("图片加载失败");
            return;
        }
        thumbnails.put(file, bitmap);
        if (tile != null) {
            tile.thumbnail().setImageBitmap(bitmap);
            tile.thumbnail().setVisibility(View.VISIBLE);
            tile.status().setVisibility(View.GONE);
        }
    }

    private void loadNextImage() {
        if (connection == null || download != null || missingImages.isEmpty()) return;
        File next = missingImages.iterator().next();
        ImageTile tile = visibleImages.get(next);
        fetchImage(tile.peer(), tile.message());
    }

    private void requestImageChunk() {
        Contact peer = download.peer();
        send("dm_image_get", "message_id", download.message().id(), "user_code", download.message().author(),
                peer.group() ? "group_id" : "target_user_code", peer.code(),
                "chunk_index", download.bytes().size() / ChatConnection.CHUNK_BYTES,
                "command_id", download.command());
    }

    private void receiveImage(JSONObject packet) throws JSONException, IOException {
        if (download == null || !download.command().equals(packet.getString("command_id"))) return;
        int offset = download.bytes().size();
        int total = download.message().imageSize();
        byte[] data;
        try {
            data = Base64.decode(packet.getString("image_data"), Base64.DEFAULT);
        } catch (IllegalArgumentException error) {
            download = null;
            throw new IOException("图片分片编码无效", error);
        }
        // The wire encoder omits chunk_index for the first chunk (index 0).
        long chunkIndex = packet.has("chunk_index") ? packet.getLong("chunk_index") : 0;
        int expectedIndex = offset / ChatConnection.CHUNK_BYTES;
        if (chunkIndex != expectedIndex || total != packet.getInt("image_size")
                || (download.peer().group() && (!download.message().id().equals(packet.getString("message_id"))
                    || !download.message().author().equalsIgnoreCase(packet.getString("user_code"))
                    || !download.peer().code().equals(packet.getString("group_id"))))
                || data.length != TransportCodec.chunkLength(total, expectedIndex)) {
            download = null;
            throw new IOException("图片分片序号或大小与预期不符");
        }
        download.bytes().write(data);
        if (download.bytes().size() < total) {
            requestImageChunk();
            return;
        }
        ImageDownload finished = download;
        File file = cachedImage(finished.peer(), finished.message());
        imageLoads.execute(() -> {
            Bitmap decoded = null;
            Path temporary = null;
            try {
                temporary = Files.createTempFile(file.getParentFile().toPath(), "image-", ".part");
                try (OutputStream output = Files.newOutputStream(temporary)) {
                    finished.bytes().writeTo(output);
                }
                Files.move(temporary, file.toPath(), StandardCopyOption.ATOMIC_MOVE);
                decoded = decodeThumbnail(file);
            } catch (IOException error) {
                String detail = error.getMessage();
                main.post(() -> toast("无法保存聊天图片 · " + detail));
            } finally {
                if (temporary != null) temporary.toFile().delete();
            }
            Bitmap thumbnail = decoded;
            main.post(() -> {
                if (isDestroyed()) return;
                showThumbnail(file, thumbnail);
                if (download == finished) download = null;
                loadNextImage();
            });
        });
    }

    private File cachedImage(Contact peer, ChatMessage row) {
        String identity = key(ownCode) + ":" + key(peer.code()) + ":" + messageKey(row);
        String name = Base64.encodeToString(identity.getBytes(java.nio.charset.StandardCharsets.UTF_8), Base64.NO_WRAP | Base64.URL_SAFE);
        return new File(getCacheDir(), name + ".image");
    }

    private void showImage(File file) {
        BitmapFactory.Options options = new BitmapFactory.Options();
        Bitmap bitmap = BitmapFactory.decodeFile(file.getPath(), options);
        if (bitmap == null) {
            toast("无法打开这张图片");
            return;
        }
        Dialog dialog = new Dialog(this, android.R.style.Theme_Material_NoActionBar);
        LinearLayout fullScreen = column();
        fullScreen.setBackgroundColor(0xff161616);
        fullScreen.setPadding(dp(16), dp(10), dp(16), dp(12));
        LinearLayout header = new LinearLayout(this);
        header.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
        fullScreen.addView(header, new LinearLayout.LayoutParams(-1, dp(52)));
        ImageView viewer = new ImageViewport(this, bitmap);
        viewer.setContentDescription("聊天图片");
        fullScreen.addView(viewer, new LinearLayout.LayoutParams(-1, 0, 1));
        String extension = "image/png".equals(options.outMimeType) ? ".png" : ".jpg";
        header.addView(button("保存", false,
                () -> chooseSaveLocation(new SavedDocument(file, "LAN Chat" + extension, options.outMimeType, false))));
        header.addView(button("分享", false, () -> shareImage(file, options.outMimeType, extension)));
        Button close = button("关闭", false, () -> {
            viewer.animate().alpha(0f).scaleX(0.97f).scaleY(0.97f)
                    .setDuration(MOTION_SURFACE).withEndAction(dialog::dismiss).start();
        });
        header.addView(close);
        fullScreen.setOnApplyWindowInsetsListener((view, insets) -> {
            if (Build.VERSION.SDK_INT >= 30) {
                Insets bars = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                view.setPadding(dp(16) + bars.left, dp(8) + bars.top, dp(16) + bars.right, dp(10) + bars.bottom);
            }
            return insets;
        });
        dialog.setContentView(fullScreen);
        dialog.show();
        dialog.getWindow().setLayout(-1, -1);
        dialog.getWindow().setStatusBarColor(0xff161616);
        dialog.getWindow().setNavigationBarColor(0xff161616);
        viewer.setScaleX(0.92f);
        viewer.setScaleY(0.92f);
        viewer.setAlpha(0.25f);
        viewer.animate().alpha(1f).scaleX(1f).scaleY(1f).setDuration(MOTION_ENTER).setInterpolator(ARRIVE).start();
    }

    private void shareImage(File source, String mime, String extension) {
        fileTransfers.execute(() -> {
            try {
                File directory = new File(getCacheDir(), "shared-images");
                Files.createDirectories(directory.toPath());
                File copy = new File(directory, source.getName() + extension);
                Files.copy(source.toPath(), copy.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING);
                Uri uri = FileProvider.getUriForFile(this, getPackageName() + ".files", copy);
                main.post(() -> {
                    if (isDestroyed() || !foreground) return;
                    Intent share = new Intent(Intent.ACTION_SEND).setType(mime).putExtra(Intent.EXTRA_STREAM, uri)
                            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                    startActivity(Intent.createChooser(share, null));
                });
            } catch (IOException | IllegalArgumentException error) {
                main.post(() -> toast("无法分享图片 · " + error.getMessage()));
            }
        });
    }

    private static final class ImageViewport extends ImageView {
        private final Matrix transform = new Matrix();
        private final Bitmap image;
        private final ScaleGestureDetector detector;
        private float fit;
        private float scale;
        private float left;
        private float top;
        private float touchX;
        private float touchY;
        private boolean dragging;

        ImageViewport(Context context, Bitmap image) {
            super(context);
            this.image = image;
            setScaleType(ScaleType.MATRIX);
            setImageBitmap(image);
            detector = new ScaleGestureDetector(context, new ScaleGestureDetector.SimpleOnScaleGestureListener() {
                @Override public boolean onScale(ScaleGestureDetector gesture) {
                    float next = Math.max(fit, Math.min(fit * 4, scale * gesture.getScaleFactor()));
                    float focusX = gesture.getFocusX();
                    float focusY = gesture.getFocusY();
                    left = focusX - (focusX - left) * next / scale;
                    top = focusY - (focusY - top) * next / scale;
                    scale = next;
                    placeImage();
                    return true;
                }
            });
        }

        @Override protected void onSizeChanged(int width, int height, int previousWidth, int previousHeight) {
            super.onSizeChanged(width, height, previousWidth, previousHeight);
            if (width == 0 || height == 0) return;
            fit = Math.min((float) width / image.getWidth(), (float) height / image.getHeight());
            scale = fit;
            placeImage();
        }

        private void placeImage() {
            float width = image.getWidth() * scale;
            float height = image.getHeight() * scale;
            left = width <= getWidth() ? (getWidth() - width) / 2 : Math.max(getWidth() - width, Math.min(0, left));
            top = height <= getHeight() ? (getHeight() - height) / 2 : Math.max(getHeight() - height, Math.min(0, top));
            transform.setScale(scale, scale);
            transform.postTranslate(left, top);
            setImageMatrix(transform);
        }

        @Override public boolean onTouchEvent(MotionEvent event) {
            detector.onTouchEvent(event);
            boolean singleTouch = event.getPointerCount() == 1;
            if (event.getActionMasked() == MotionEvent.ACTION_MOVE && singleTouch && dragging && scale > fit) {
                left += event.getX() - touchX;
                top += event.getY() - touchY;
                placeImage();
            }
            dragging = singleTouch;
            touchX = event.getX();
            touchY = event.getY();
            if (event.getActionMasked() == MotionEvent.ACTION_UP) performClick();
            return true;
        }

        @Override public boolean performClick() {
            super.performClick();
            return true;
        }
    }

    private void showSettings() {
        releaseChatPanels();
        socialBack = this::restoreHome;
        friendRows = null;
        groupRows = null;
        groupDetails = null;
        settings = true;
        active = null;
        visibleImages.clear();
        missingImages.clear();
        contactRows = null;
        visibleMessageTimes.clear();
        page = newPage();
        LinearLayout header = bar("设置");
        header.addView(iconButton("返回", R.drawable.ic_back, false, this::restoreHome), 0);
        page.addView(header);
        ScrollView settingsScroll = new ScrollView(this);
        settingsScroll.setVerticalScrollBarEnabled(false);
        page.addView(settingsScroll, new LinearLayout.LayoutParams(-1, 0, 1));
        LinearLayout options = column();
        options.setPadding(0, dp(14), 0, dp(12));
        settingsScroll.addView(options, new ScrollView.LayoutParams(-1, -2));

        LinearLayout identity = new LinearLayout(this);
        identity.setGravity(Gravity.CENTER_VERTICAL);
        identity.setPadding(dp(14), dp(15), dp(14), dp(15));
        identity.setBackground(card(WHITE, 18));
        identity.addView(avatar(username), new LinearLayout.LayoutParams(dp(46), dp(46)));
        LinearLayout names = column();
        TextView displayName = label(username, 17, INK);
        displayName.setTypeface(mediumFont);
        names.addView(displayName);
        addSpaced(names, label("用户代码  " + ownCode, 13, MUTED), 3);
        LinearLayout.LayoutParams details = new LinearLayout.LayoutParams(0, -2, 1);
        details.setMarginStart(dp(12));
        identity.addView(names, details);
        options.addView(identity);

        TextView appearanceTitle = label("外观", 13, MUTED);
        appearanceTitle.setPadding(dp(4), 0, 0, 0);
        addSpaced(options, appearanceTitle, 24);
        addSpaced(options, themeSlider(), 8);

        TextView motionTitle = label("动效", 13, MUTED);
        motionTitle.setPadding(dp(4), 0, 0, 0);
        addSpaced(options, motionTitle, 24);
        addSpaced(options, labeledSlider("新文字淡入",
                motionSlider(PREF_NEW_TEXT, "fade", new String[]{"fade", "instant"},
                        new String[]{"淡入", "直接出现"})), 8);
        addSpaced(options, labeledSlider("消息气泡动画",
                motionSlider(PREF_BUBBLE, "fly", new String[]{"grow", "fly"},
                        new String[]{"原地长出", "从输入框飞上来"})), 16);
        addSpaced(options, labeledSlider("聊天区滚动动画",
                motionSlider(PREF_SCROLL, "smooth", new String[]{"smooth", "jump"},
                        new String[]{"跟随滑动", "直接跳到"})), 16);
        Switch caretMotion = new Switch(this);
        caretMotion.setText("输入光标跟随");
        caretMotion.setTextSize(16);
        caretMotion.setTextColor(INK);
        caretMotion.setChecked(preferences().getBoolean(PREF_CARET, true));
        caretMotion.setOnCheckedChangeListener((button, checked) -> preferences().edit().putBoolean(PREF_CARET, checked).apply());
        addSpaced(options, caretMotion, 16);

        TextView notificationTitle = label("通知", 13, MUTED);
        notificationTitle.setPadding(dp(4), 0, 0, 0);
        addSpaced(options, notificationTitle, 24);
        Switch permission = new Switch(this);
        permission.setText("消息系统通知");
        permission.setTextSize(16);
        permission.setTextColor(INK);
        permission.setMinHeight(dp(60));
        permission.setPadding(dp(14), dp(8), dp(14), dp(8));
        permission.setThumbTintList(ColorStateList.valueOf(WHITE));
        permission.setTrackTintList(new ColorStateList(
                new int[][]{new int[]{android.R.attr.state_checked}, new int[]{}},
                new int[]{ACCENT, OUTLINE}));
        permission.setChecked(preferences().getBoolean(MessagePushService.ENABLED, false));
        permission.setOnCheckedChangeListener((button, enabled) -> {
            preferences().edit().putBoolean(MessagePushService.ENABLED, enabled).apply();
            syncNotifications(true);
        });
        permission.setBackground(ripple(card(WHITE, 18), false));
        addSpaced(options, permission, 8);
        addSpaced(options, button("机器人设置", false, this::showBotSettings), 16);
        Button logout = button("退出登录", false, () -> {
            syncNotificationsBeforeLogout();
            forgetAccount();
            clearSession();
            registering = false;
            showLogin("");
        });
        logout.setTextColor(DANGER);
        addSpaced(options, logout, 26);
        enter(options, 12, 0, 260);
    }

    private void showBotSettings() {
        LinearLayout body = socialPage("机器人设置", this::showSettings, false);
        new BotSettings(body).account(false);
    }

    private final class BotSettings {
        private final LinearLayout screen = page;
        private final List<View> controls = new ArrayList<>();
        private final Map<AstrBot.Resource, EditText> ids = new LinkedHashMap<>();
        private final EditText endpoint;
        private final EditText token;
        private final TextView status = label("", 13, MUTED);
        private final Button saveAccount;
        private long revision = -1;
        private boolean busy;

        BotSettings(LinearLayout body) {
            body.setPadding(0, dp(14), 0, dp(16));
            endpoint = botField(body, "AstrBot 地址", InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
            endpoint.setHint("https://bot.example.com");
            token = botField(body, "AstrBot OpenAPI Key", InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
            endpoint.addTextChangedListener(new TextWatcher() {
                @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
                @Override public void onTextChanged(CharSequence s, int start, int before, int count) {
                    token.setText("");
                }
                @Override public void afterTextChanged(Editable s) {}
            });
            addSpaced(body, label(botKeySyncAvailable
                    ? "保存到账号后，密钥会加密同步到同账号设备。清空并保存可移除已同步的密钥。"
                    : "使用拥有 provider 和 persona 权限的 OpenAPI Key。当前服务器同步地址和配置编号。", 13, MUTED), 6);
            controls.add(endpoint);
            controls.add(token);

            for (AstrBot.Resource resource : AstrBot.Resource.values()) {
                String title = title(resource);
                LinearLayout card = column();
                card.setPadding(dp(12), dp(4), dp(12), dp(12));
                card.setBackground(card(WHITE, 18));
                addSpaced(body, card, 14);
                EditText id = botField(card, title + "编号", InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
                ids.put(resource, id);
                controls.add(id);
                LinearLayout actions = new LinearLayout(MainActivity.this);
                Button list = button("读取列表", false, () -> list(resource));
                Button read = button("读取配置", false, () -> open(resource, false));
                Button create = button("新建", false, () -> open(resource, true));
                for (Button action : new Button[]{list, read, create}) {
                    controls.add(action);
                    tabAction(actions, action);
                }
                addSpaced(card, actions, 8);
            }
            LinearLayout accountActions = new LinearLayout(MainActivity.this);
            Button reload = button("读取账号设置", false, () -> account(false));
            saveAccount = button("保存到账号", true, () -> account(true));
            saveAccount.setEnabled(false);
            controls.add(reload);
            tabAction(accountActions, reload);
            tabAction(accountActions, saveAccount);
            addSpaced(body, accountActions, 16);
            status.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
            addSpaced(body, status, 8);
        }

        private String title(AstrBot.Resource resource) {
            return switch (resource) {
                case SOURCE -> "模型源";
                case PROVIDER -> "模型";
                case PERSONA -> "人格";
            };
        }

        void account(boolean save) {
            if (busy) return;
            ChatConnection session = connection;
            if (session == null || !botSettingsAvailable) {
                status.setText(session == null ? "连接尚未恢复，请稍后重试" : "服务器不支持机器人设置同步");
                return;
            }
            try {
                boolean withKey = botKeySyncAvailable;
                String kind = save ? "bot_settings_save" : "bot_settings_get";
                JSONObject request = packet(withKey ? kind + "_with_key" : kind);
                if (save) {
                    JSONObject content = new JSONObject().put("endpoint", endpoint.getText().toString().trim());
                    for (var entry : ids.entrySet()) {
                        content.put(entry.getKey().idField, entry.getValue().getText().toString());
                    }
                    if (withKey) {
                        content = new JSONObject().put("settings", content)
                                .put("openapi_key", token.getText().toString().trim());
                    }
                    request.put("revision", revision).put("content", content.toString());
                }
                run(() -> session.request(request), response -> {
                    JSONObject envelope = new JSONObject(response.getString("content"));
                    JSONObject content = withKey ? envelope.getJSONObject("settings") : envelope;
                    String url = content.getString("endpoint");
                    if (!url.contentEquals(endpoint.getText())) endpoint.setText(url);
                    if (withKey) token.setText(envelope.getString("openapi_key"));
                    for (var entry : ids.entrySet()) {
                        entry.getValue().setText(content.getString(entry.getKey().idField));
                    }
                    revision = response.optLong("revision", 0);
                    status.setText("账号设置已同步");
                }, status, save ? "正在保存账号设置…" : "正在读取账号设置…");
            } catch (JSONException error) {
                status.setText("账号设置格式无效");
            }
        }

        private void list(AstrBot.Resource resource) {
            String url = endpoint.getText().toString().trim();
            String key = token.getText().toString().trim();
            run(() -> new JSONObject().put("ids", new JSONArray(Arrays.asList(new AstrBot(url, key).list(resource)))), response -> {
                JSONArray values = response.getJSONArray("ids");
                if (values.length() == 0) {
                    status.setText("请填写编号新建配置");
                    return;
                }
                String[] options = new String[values.length()];
                for (int i = 0; i < options.length; i++) options[i] = values.getString(i);
                status.setText("");
                new AlertDialog.Builder(MainActivity.this).setTitle("选择" + title(resource))
                        .setItems(options, (dialog, index) -> ids.get(resource).setText(options[index]))
                        .setNegativeButton("取消", null).show();
            }, status, "正在联系 AstrBot…");
        }

        private void open(AstrBot.Resource resource, boolean create) {
            String id = ids.get(resource).getText().toString();
            if (id.trim().isEmpty()) {
                status.setText("请填写配置编号");
                return;
            }
            String url = endpoint.getText().toString().trim();
            String key = token.getText().toString().trim();
            String sourceId = ids.get(AstrBot.Resource.SOURCE).getText().toString();
            if (create) {
                editor(resource, url, key, id, sourceId, true, new JSONObject());
            } else {
                run(() -> new AstrBot(url, key).read(resource, id), fields -> {
                    status.setText("");
                    editor(resource, url, key, id, sourceId, false, fields);
                }, status, "正在联系 AstrBot…");
            }
        }

        private void editor(AstrBot.Resource resource, String url, String key, String id,
                            String sourceId, boolean create, JSONObject data) {
            ScrollView scroll = new ScrollView(MainActivity.this);
            LinearLayout form = column();
            form.setPadding(dp(20), 0, dp(20), dp(12));
            scroll.addView(form);
            Map<String, EditText> fields = new LinkedHashMap<>();
            switch (resource) {
                case SOURCE -> {
                    fields.put("api_base", botField(form, "模型 API 地址", InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI));
                    EditText secret = botField(form, "模型 API Key", InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
                    if (!create) secret.setHint("留空保留现有密钥");
                    fields.put("api_key", secret);
                }
                case PROVIDER -> fields.put("model", botField(form, "模型名称", InputType.TYPE_CLASS_TEXT));
                case PERSONA -> {
                    int multiline = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE;
                    fields.put("system_prompt", botField(form, "系统提示词", multiline));
                    fields.put("custom_error_message", botField(form, "自定义出错回复", multiline));
                }
            }
            for (var entry : fields.entrySet()) entry.getValue().setText(data.optString(entry.getKey(), ""));
            TextView outcome = label("", 13, MUTED);
            outcome.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
            addSpaced(form, outcome, 8);
            String title = create && resource == AstrBot.Resource.SOURCE ? "新建 OpenAI 兼容模型源"
                    : (create ? "新建" : "编辑") + title(resource);
            AlertDialog dialog = new AlertDialog.Builder(MainActivity.this).setTitle(title).setView(scroll)
                    .setNegativeButton("取消", null).setPositiveButton(create ? "创建到 AstrBot" : "保存到 AstrBot", null).create();
            dialog.setOnShowListener(ignored -> dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view -> {
                if (busy) return;
                try {
                    JSONObject values = new JSONObject().put("source_id", sourceId);
                    for (var entry : fields.entrySet()) values.put(entry.getKey(), entry.getValue().getText().toString());
                    run(() -> {
                        new AstrBot(url, key).save(resource, id, values, create);
                        return new JSONObject();
                    }, result -> {
                        dialog.dismiss();
                        status.setText("已保存到 AstrBot");
                    }, outcome, "正在保存到 AstrBot…");
                } catch (JSONException error) {
                    outcome.setText("表单格式无效");
                }
            }));
            dialog.show();
        }

        private void run(BotRequest task, SocialReply accept, TextView outcome, String pending) {
            if (busy) return;
            busy = true;
            controls.forEach(control -> control.setEnabled(false));
            saveAccount.setEnabled(false);
            outcome.setText(pending);
            socialRequests.execute(() -> {
                try {
                    JSONObject result = task.call();
                    main.post(() -> complete(() -> accept.accept(result), outcome));
                } catch (IOException | JSONException error) {
                    String message = error instanceof IOException ? error.getMessage() : "机器人配置响应无效";
                    main.post(() -> complete(() -> outcome.setText(message), outcome));
                }
            });
        }

        private void complete(SocialReplyCompletion result, TextView outcome) {
            if (isDestroyed() || page != screen) return;
            try {
                result.accept();
            } catch (JSONException error) {
                outcome.setText("机器人配置响应无效");
            } finally {
                busy = false;
                controls.forEach(control -> control.setEnabled(true));
                saveAccount.setEnabled(revision >= 0);
            }
        }
    }

    private interface SocialReplyCompletion {
        void accept() throws JSONException;
    }

    private interface BotRequest {
        JSONObject call() throws IOException, JSONException;
    }

    private EditText botField(LinearLayout parent, String name, int inputType) {
        EditText input = field("", inputType);
        input.setId(View.generateViewId());
        input.setSaveEnabled(false);
        input.setImportantForAutofill(View.IMPORTANT_FOR_AUTOFILL_NO);
        TextView caption = label(name, 14, INK);
        caption.setLabelFor(input.getId());
        addSpaced(parent, caption, 8);
        addSpaced(parent, input, 4);
        return input;
    }

    private SharedPreferences preferences() {
        return getSharedPreferences(MessagePushService.PREFERENCES, MODE_PRIVATE);
    }

    /** App 内主题偏好：system / light / dark。视图全在 onCreate 里建，所以在 attachBaseContext 就定好 uiMode。 */
    private static final String PREF_THEME = "appearance_theme";
    /** 动效偏好，与桌面设置页「动效」对应。 */
    private static final String PREF_NEW_TEXT = "motion_new_text";
    private static final String PREF_BUBBLE = "motion_bubble";
    private static final String PREF_SCROLL = "motion_scroll";
    private static final String PREF_CARET = "motion_caret";
    /** 主题切换重建 Activity 后，下一次进入主页时改回设置页。 */
    private static boolean reopenSettingsAfterRecreate;

    /** 新文字：fade（淡入，默认）/ instant（直接出现）。 */
    private boolean fadeInNewText() {
        return !"instant".equals(preferences().getString(PREF_NEW_TEXT, "fade"));
    }

    /** 气泡：grow（原地长出）/ fly（从输入框飞上来，默认）。 */
    private boolean growBubble() {
        return "grow".equals(preferences().getString(PREF_BUBBLE, "fly"));
    }

    /** 滚动：smooth（跟随滑动，默认）/ jump（直接跳到底部）。 */
    private boolean smoothScrollEnabled() {
        return !"jump".equals(preferences().getString(PREF_SCROLL, "smooth"));
    }

    @Override
    protected void attachBaseContext(Context base) {
        String theme = base.getSharedPreferences(MessagePushService.PREFERENCES, MODE_PRIVATE)
                .getString(PREF_THEME, "system");
        int night = "dark".equals(theme) ? Configuration.UI_MODE_NIGHT_YES
                : "light".equals(theme) ? Configuration.UI_MODE_NIGHT_NO
                : Configuration.UI_MODE_NIGHT_UNDEFINED;
        if (night == Configuration.UI_MODE_NIGHT_UNDEFINED) {
            super.attachBaseContext(base);
            return;
        }
        Configuration configuration = new Configuration(base.getResources().getConfiguration());
        configuration.uiMode = (configuration.uiMode & ~Configuration.UI_MODE_NIGHT_MASK) | night;
        super.attachBaseContext(base.createConfigurationContext(configuration));
    }

    /** 外观主题：三档滑块（跟随系统 / 浅色 / 深色），与桌面设置页的 SegmentedRadio 同形制。 */
    private View themeSlider() {
        return segmentedSlider(PREF_THEME, "system", new String[]{"system", "light", "dark"},
                new String[]{"跟随系统", "浅色", "深色"}, () -> {
                    // 主题在 attachBaseContext 定稿，重建 Activity 让资源与对话框配色一起切过去。
                    reopenSettingsAfterRecreate = true;
                    recreate();
                });
    }

    /** 带小标题的滑块行：标题 14px，滑块距标题 6px。 */
    private View labeledSlider(String text, View slider) {
        LinearLayout row = column();
        row.addView(label(text, 14, INK));
        addSpaced(row, slider, 6);
        return row;
    }

    /** 动效偏好滑块：写入即生效，下一条消息 / 下一次滚动就按新值执行。 */
    private View motionSlider(String key, String fallback, String[] values, String[] texts) {
        return segmentedSlider(key, fallback, values, texts, null);
    }

    /** 三档滑块本体：TINT 轨道 + PAPER 胶囊，点击后胶囊滑到选中项（160ms，与桌面 surface 档一致）。 */
    private View segmentedSlider(String key, String fallback, String[] values, String[] texts, Runnable afterChange) {
        final String current = preferences().getString(key, fallback);
        FrameLayout holder = new FrameLayout(this);
        holder.setBackground(card(TINT, 18));
        holder.setPadding(dp(4), dp(4), dp(4), dp(4));
        final View pill = new View(this);
        pill.setBackground(surface(PAPER, 15));
        holder.addView(pill, new FrameLayout.LayoutParams(0, dp(40)));
        final TextView[] items = new TextView[texts.length];
        LinearLayout labels = new LinearLayout(this);
        labels.setOrientation(LinearLayout.HORIZONTAL);
        for (int i = 0; i < texts.length; i++) {
            final String value = values[i];
            boolean selected = value.equals(current);
            TextView item = label(texts[i], 14, selected ? INK : MUTED);
            item.setGravity(Gravity.CENTER);
            if (selected) item.setTypeface(mediumFont);
            item.setOnClickListener(view -> {
                if (value.equals(preferences().getString(key, fallback))) return;
                preferences().edit().putString(key, value).apply();
                int index = 0;
                for (int candidate = 0; candidate < values.length; candidate++) {
                    boolean activeItem = values[candidate].equals(value);
                    items[candidate].setTextColor(activeItem ? INK : MUTED);
                    items[candidate].setTypeface(activeItem ? mediumFont : regularFont);
                    if (activeItem) index = candidate;
                }
                int width = holder.getWidth() - holder.getPaddingLeft() - holder.getPaddingRight();
                pill.animate().translationX((width / (float) texts.length) * index)
                        .setDuration(MOTION_SURFACE).setInterpolator(ARRIVE).start();
                if (afterChange != null) afterChange.run();
            });
            item.setBackground(ripple(surface(Color.TRANSPARENT, 15), false));
            labels.addView(item, new LinearLayout.LayoutParams(0, dp(40), 1));
            items[i] = item;
        }
        holder.addView(labels, new FrameLayout.LayoutParams(-1, dp(40)));
        holder.addOnLayoutChangeListener((view, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> {
            int width = right - left - holder.getPaddingLeft() - holder.getPaddingRight();
            FrameLayout.LayoutParams pillParams = (FrameLayout.LayoutParams) pill.getLayoutParams();
            pillParams.width = width / texts.length;
            pill.setLayoutParams(pillParams);
            String saved = preferences().getString(key, fallback);
            int index = 0;
            for (int i = 0; i < values.length; i++) if (values[i].equals(saved)) index = i;
            pill.setTranslationX((width / (float) texts.length) * index);
        });
        return holder;
    }

    private void syncNotifications(boolean requestPermission) {
        if (connection == null) return;
        boolean enabled = preferences().getBoolean(MessagePushService.ENABLED, false);
        if (!enabled && !requestPermission) return;
        if (enabled && !MessagePushService.hasPermission(this)) {
            if (requestPermission && Build.VERSION.SDK_INT >= 33) {
                requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, NOTIFICATION_PERMISSION);
            }
            return;
        }
        if (FirebaseApp.getApps(this).isEmpty()) {
            if (enabled) toast("此安装包尚未配置 Firebase 推送");
            return;
        }
        ChatConnection session = connection;
        FirebaseMessaging.getInstance().getToken().addOnCompleteListener(result -> {
            if (connection != session || preferences().getBoolean(MessagePushService.ENABLED, false) != enabled) return;
            if (!result.isSuccessful()) {
                if (enabled) toast("系统推送注册失败 · " + result.getException());
                return;
            }
            send("dm_push_register", "content", result.getResult(), "enabled", enabled);
        });
    }

    private void syncNotificationsBeforeLogout() {
        preferences().edit().putBoolean(MessagePushService.ENABLED, false).remove(MessagePushService.ACCOUNT).apply();
        if (!FirebaseApp.getApps(this).isEmpty()) FirebaseMessaging.getInstance().deleteToken();
    }

    @Override public void onRequestPermissionsResult(int request, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(request, permissions, results);
        if (request != NOTIFICATION_PERMISSION) return;
        if (!MessagePushService.hasPermission(this)) preferences().edit().putBoolean(MessagePushService.ENABLED, false).apply();
        syncNotifications(false);
        if (settings) showSettings();
    }

    private void send(String type, Object... fields) {
        ChatConnection session = connection;
        if (session == null) { toast("连接尚未恢复，请稍后重试"); return; }
        writers.execute(() -> {
            try {
                session.send(packet(type, fields));
            } catch (IOException | JSONException error) {
                main.post(() -> {
                    toast("请求发送失败 · " + error.getMessage());
                    if (connection == session && error instanceof IOException) {
                        disconnect();
                        reconnect.run();
                    }
                });
            }
        });
    }

    private static JSONObject packet(String type, Object... fields) throws JSONException {
        JSONObject packet = ChatConnection.packet(type);
        for (int index = 0; index < fields.length; index += 2) packet.put((String) fields[index], fields[index + 1]);
        return packet;
    }

    private boolean isActive(String peer) { return active != null && active.code().equalsIgnoreCase(peer); }
    private static String key(String value) { return value.toLowerCase(Locale.ROOT); }
    private static String messageKey(ChatMessage row) { return key(row.author()) + ":" + row.id(); }

    private void forgetAccount() {
        savedAccount = null;
        pendingImageUri = null;
        pendingFileUri = null;
        SessionCredentials.clear(this);
    }

    private void closeConnection(ChatConnection session) {
        // Closing a TLS socket may send close_notify, even without an active upload.
        new Thread(() -> {
            try {
                session.close();
            } catch (IOException error) {
                main.post(() -> {
                    if (foreground && !isDestroyed()) toast("关闭连接失败 · " + error.getMessage());
                });
            }
        }, "LAN Chat closer").start();
    }

    private void disconnect() {
        pendingHistory.clear();
        if (chatSearch != null) chatSearch.connectionLost();
        MessagePushService.foregroundConnected = false;
        ++generation;
        main.removeCallbacks(reconnect);
        main.removeCallbacks(refreshContacts);
        ChatConnection previous = connection;
        connection = null;
        authenticating = false;
        download = null;
        for (Map.Entry<String, PendingText> entry : pendingTexts.entrySet()) {
            updatePendingStatus(entry.getKey(), entry.getValue(), "failed");
        }
        if (previous != null) closeConnection(previous);
    }

    private void clearSession() {
        releaseChatPanels();
        socialBack = null;
        friendRows = null;
        groupRows = null;
        groupDetails = null;
        historyCursor.clear();
        initializedHistory.clear();
        disconnect();
        active = null;
        settings = false;
        contacts.clear();
        groupDirectory.clear();
        histories.clear();
        pendingTexts.clear();
        contactRows = null;
        visibleMessageTimes.clear();
        visibleImages.clear();
        missingImages.clear();
        thumbnails.evictAll();
        ownCode = null;
        connectionStatus = "";
    }

    @Override public void onBackPressed() {
        if (ownCode != null && (settings || active != null || socialBack != null)) handleSystemBack();
        else super.onBackPressed();
    }

    private void handleSystemBack() {
        if (Build.VERSION.SDK_INT >= 30) {
            View decor = getWindow().getDecorView();
            WindowInsets insets = decor.getRootWindowInsets();
            if (insets != null && insets.isVisible(WindowInsets.Type.ime())) {
                getWindow().getInsetsController().hide(WindowInsets.Type.ime());
                return;
            }
        }
        if (composerPanels != null && composerPanels.hide()) return;
        if (chatSearch != null && chatSearch.open) chatSearch.setOpen(false);
        else if (socialBack != null) socialBack.run();
        else showHome(false);
    }

    private void updateSystemBack() {
        if (Build.VERSION.SDK_INT < 33) return;
        if (active != null || settings || socialBack != null) {
            if (backCallback == null) {
                backCallback = this::handleSystemBack;
                getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                        OnBackInvokedDispatcher.PRIORITY_DEFAULT, backCallback);
            }
        } else if (backCallback != null) {
            getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(backCallback);
            backCallback = null;
        }
    }

    @Override protected void onDestroy() {
        clearSession();
        updateSystemBack();
        hideTransferChip();
        writers.shutdownNow();
        fileTransfers.shutdownNow();
        imageLoads.shutdownNow();
        socialRequests.shutdownNow();
        super.onDestroy();
    }

    private LinearLayout newPage() {
        return newPage(0);
    }

    private LinearLayout newPage(int direction) {
        return newPage(direction, false);
    }

    private LinearLayout newPage(int direction, boolean slideFromLeftEdge) {
        LinearLayout result = column();
        result.setBackground(new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,
                new int[]{TINT, PAPER, PAPER}));
        result.setFocusableInTouchMode(true);
        if (ownCode != null) {
            connectionBanner = label(connectionStatus, 12, DANGER);
            connectionBanner.setPadding(dp(8), dp(4), dp(8), dp(6));
            connectionBanner.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
            connectionBanner.setVisibility(connectionStatus.isEmpty() ? View.GONE : View.VISIBLE);
            result.addView(connectionBanner);
        }
        result.setOnApplyWindowInsetsListener((view, windowInsets) -> {
            if (Build.VERSION.SDK_INT >= 30) {
                int lightBars = WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS
                        | WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;
                int barAppearance = DARK ? 0 : lightBars;
                WindowInsetsController controller = view.getWindowInsetsController();
                if (controller != null) controller.setSystemBarsAppearance(barAppearance, lightBars);
                Insets insets = windowInsets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout() | WindowInsets.Type.ime());
                int bottom = dp(10) + insets.bottom;
                boolean stickToLatest = result.getPaddingBottom() != bottom
                        && active != null && chatBody != null && chatBody.getParent() == result && scroll.following;
                result.setPadding(dp(16) + insets.left, dp(8) + insets.top, dp(16) + insets.right, bottom);
                if (stickToLatest) followConversation(false, 0, true, 0);
            }
            return windowInsets;
        });
        result.setPadding(dp(16), dp(8), dp(16), dp(10));
        if (screen == null) {
            screen = new FrameLayout(this);
            setContentView(screen);
        }
        View previous = screen.getChildCount() == 0 ? null : screen.getChildAt(screen.getChildCount() - 1);
        if (direction != 0 && previous != null && previous.isLaidOut()) {
            while (screen.getChildCount() > 1) {
                screen.getChildAt(0).animate().cancel();
                screen.removeViewAt(0);
            }
            previous.animate().cancel();
            previous.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS);
            result.setTranslationX(slideFromLeftEdge ? -previous.getWidth() : direction * dp(20));
            result.getViewTreeObserver().addOnPreDrawListener(new ViewTreeObserver.OnPreDrawListener() {
                @Override public boolean onPreDraw() {
                    result.getViewTreeObserver().removeOnPreDrawListener(this);
                    result.postOnAnimation(() -> {
                        if (screen.getChildAt(screen.getChildCount() - 1) != result) return;
                        if (!slideFromLeftEdge) {
                            previous.animate().translationX(-direction * dp(8)).setDuration(MOTION_OPEN).setInterpolator(ARRIVE).start();
                        }
                        result.animate().translationX(0).setDuration(slideFromLeftEdge ? MOTION_ENTER : MOTION_OPEN).setInterpolator(ARRIVE)
                                .withEndAction(() -> screen.removeView(previous)).start();
                    });
                    return true;
                }
            });
        } else {
            for (int index = screen.getChildCount() - 1; index >= 0; index--) {
                screen.getChildAt(index).animate().cancel();
            }
            screen.removeAllViews();
        }
        screen.addView(result, new FrameLayout.LayoutParams(-1, -1));
        updateSystemBack();
        result.requestFocus();
        result.requestApplyInsets();
        return result;
    }

    private LinearLayout bar(String text) {
        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setMinimumHeight(dp(59));
        row.setPadding(dp(7), dp(5), dp(7), dp(5));
        row.setBackground(card(WHITE, 20));
        TextView heading = title(text);
        heading.setTextSize(22);
        heading.setMaxLines(1);
        heading.setEllipsize(TextUtils.TruncateAt.END);
        heading.setPadding(dp(8), 0, dp(8), 0);
        row.addView(heading, new LinearLayout.LayoutParams(0, -2, 1));
        return row;
    }

    private TextView avatar(String name) {
        String initial = name.substring(0, name.offsetByCodePoints(0, 1));
        TextView avatar = label(initial.toUpperCase(Locale.ROOT), 16, ACCENT);
        avatar.setTypeface(mediumFont);
        avatar.setGravity(Gravity.CENTER);
        avatar.setBackground(surface(TINT, 16));
        avatar.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        return avatar;
    }

    private void enter(View view, int x, int y, long duration) {
        view.setAlpha(0f);
        view.setTranslationX(dp(x));
        view.setTranslationY(dp(y));
        view.animate().alpha(1f).translationX(0).translationY(0).setDuration(duration)
                .setInterpolator(ARRIVE).start();
    }

    private LinearLayout column() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        return layout;
    }

    private void addSpaced(LinearLayout parent, View view, int spacing) {
        LinearLayout.LayoutParams margin = new LinearLayout.LayoutParams(-1, -2);
        margin.topMargin = dp(spacing);
        parent.addView(view, margin);
    }

    private TextView title(String text) {
        TextView title = label(text, 27, INK);
        title.setTypeface(boldFont);
        return title;
    }

    private TextView label(String text, int size, int color) {
        TextView view = new TextView(this);
        view.setTypeface(regularFont);
        view.setText(text);
        view.setTextSize(size);
        view.setTextColor(color);
        return view;
    }

    private Button button(String text, boolean primary, Runnable action) {
        Button button = new Button(this);
        button.setText(text);
        button.setTextColor(controlInk(primary));
        button.setAllCaps(false);
        button.setTextSize(14);
        button.setTypeface(mediumFont);
        button.setMinimumWidth(0);
        button.setMinWidth(0);
        button.setMinHeight(dp(48));
        button.setPadding(dp(13), 0, dp(13), 0);
        button.setBackgroundTintList(null);
        button.setBackground(controlBackground(primary));
        button.setOnClickListener(view -> action.run());
        return button;
    }

    private ImageButton iconButton(String description, int drawable, boolean primary, Runnable action) {
        ImageButton button = new ImageButton(this);
        button.setLayoutParams(new LinearLayout.LayoutParams(dp(48), dp(48)));
        button.setContentDescription(description);
        button.setTooltipText(description);
        button.setImageResource(drawable);
        button.setScaleType(ImageView.ScaleType.CENTER);
        button.setImageTintList(controlInk(primary));
        button.setBackgroundTintList(null);
        button.setBackground(controlBackground(primary));
        button.setPadding(0, 0, 0, 0);
        button.setOnClickListener(view -> action.run());
        return button;
    }

    private ColorStateList controlInk(boolean primary) {
        return new ColorStateList(new int[][]{new int[]{-android.R.attr.state_enabled}, new int[]{}},
                new int[]{DISABLED_INK, primary ? Color.WHITE : ACCENT});
    }

    private RippleDrawable controlBackground(boolean primary) {
        StateListDrawable shape = new StateListDrawable();
        shape.addState(new int[]{-android.R.attr.state_enabled}, surface(TINT, 14));
        shape.addState(new int[]{}, surface(primary ? ACCENT_FILL : TINT, 14));
        return ripple(shape, primary);
    }

    private EditText field(String placeholder, int inputType) {
        EditText text = new MotionEditText(this, this::fadeInNewText,
                () -> preferences().getBoolean(PREF_CARET, true), MOTION_FEEDBACK);
        text.setHint(placeholder);
        text.setInputType(inputType);
        text.setTypeface(regularFont);
        text.setTextSize(16);
        text.setTextColor(INK);
        text.setHintTextColor(MUTED);
        text.setMinHeight(dp(48));
        text.setPadding(dp(12), dp(8), dp(12), dp(8));
        StateListDrawable shape = new StateListDrawable();
        GradientDrawable focused = surface(WHITE, 13);
        focused.setStroke(dp(1), ACCENT);
        shape.addState(new int[]{android.R.attr.state_focused}, focused);
        shape.addState(new int[]{}, card(PAPER, 13));
        text.setBackground(shape);
        return text;
    }

    private GradientDrawable card(int color, int radius) {
        GradientDrawable shape = surface(color, radius);
        shape.setStroke(dp(1), OUTLINE);
        return shape;
    }

    private LayerDrawable bookCard(int radius) {
        GradientDrawable page = new GradientDrawable(GradientDrawable.Orientation.TL_BR,
                new int[]{PAPER, WHITE});
        page.setCornerRadius(dp(radius));
        page.setStroke(dp(1), OUTLINE);
        LayerDrawable book = new LayerDrawable(new Drawable[]{
                card(OUTLINE, radius), card(SOFT_CARD, radius), page
        });
        book.setPaddingMode(LayerDrawable.PADDING_MODE_STACK);
        book.setLayerInset(0, 0, dp(4), 0, 0);
        book.setLayerInset(1, 0, dp(2), dp(1), dp(2));
        book.setLayerInset(2, 0, 0, dp(2), dp(4));
        return book;
    }

    private RippleDrawable ripple(android.graphics.drawable.Drawable content, boolean primary) {
        return new RippleDrawable(ColorStateList.valueOf(primary ? RIPPLE_PRIMARY : RIPPLE_ACCENT), content, null);
    }

    private GradientDrawable surface(int color, int radius) {
        GradientDrawable surface = new GradientDrawable();
        surface.setColor(color);
        surface.setCornerRadius(dp(radius));
        return surface;
    }

    private Typeface chatTypeface(int weight) {
        if (Build.VERSION.SDK_INT == 28) {
            return Typeface.create(Typeface.createFromAsset(getAssets(), "CascadiaNextSC.wght.ttf"), weight, false);
        }
        try {
            Font latin = new Font.Builder(getAssets(), "CascadiaCode.ttf").setFontVariationSettings("'wght' " + weight).build();
            Font chinese = new Font.Builder(getAssets(), "CascadiaNextSC.wght.ttf").setFontVariationSettings("'wght' " + weight).build();
            return new Typeface.CustomFallbackBuilder(new FontFamily.Builder(latin).build())
                    .addCustomFallback(new FontFamily.Builder(chinese).build())
                    .setSystemFallback("sans-serif").setStyle(new FontStyle(weight, FontStyle.FONT_SLANT_UPRIGHT)).build();
        } catch (IOException error) {
            throw new IllegalStateException("无法读取内置字体", error);
        }
    }

    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private void toast(String text) { Toast.makeText(this, text, Toast.LENGTH_SHORT).show(); }
}
