package dev.dusk1ng.lanchat;

import android.animation.ValueAnimator;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Picture;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffXfermode;
import android.graphics.Rect;
import android.os.SystemClock;
import android.text.Editable;
import android.text.InputFilter;
import android.text.Layout;
import android.text.Spanned;
import android.text.TextWatcher;
import android.text.method.PasswordTransformationMethod;
import android.text.style.ReplacementSpan;
import android.view.animation.PathInterpolator;
import android.view.inputmethod.BaseInputConnection;
import android.widget.EditText;

import java.util.ArrayList;
import java.util.function.BooleanSupplier;
import java.util.regex.Pattern;

/** Renders input motion over the native editor's layout and selection. */
final class MotionEditText extends EditText {
    private static final Pattern PINYIN = Pattern.compile("[a-zü]+(?:[\\p{Zs}'’]+[a-zü]*)+");
    private static final Pattern PINYIN_BREAK = Pattern.compile("[\\p{Zs}'’]+");
    private static final int PREEDIT_SPAN_FLAGS = Spanned.SPAN_EXCLUSIVE_EXCLUSIVE | Spanned.SPAN_COMPOSING;

    private record Edit(int start, int removed, int added, int prefix, int suffix) {}
    private record Insertion(int start, int end, long time) {}
    private record Deletion(Picture picture, int left, int top, int alpha, long time) {}

    private final BooleanSupplier animateText;
    private final BooleanSupplier animateCaret;
    private final long duration;
    private final PathInterpolator ease = new PathInterpolator(.22f, 1f, .36f, 1f);
    private final Paint erase = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint cursorPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();
    private final ArrayList<Insertion> inserts = new ArrayList<>();
    private final ArrayList<Deletion> deletes = new ArrayList<>();
    private Edit filteredEdit;
    private Edit preparedEdit;
    private boolean cursorReady;
    private boolean movingCursor;
    private float fromX, fromTop, fromBottom, toX, toTop, toBottom;
    private long cursorStart;

    MotionEditText(Context context, BooleanSupplier animateText, BooleanSupplier animateCaret, long duration) {
        super(context);
        this.animateText = animateText;
        this.animateCaret = animateCaret;
        this.duration = duration;
        erase.setXfermode(new PorterDuffXfermode(PorterDuff.Mode.DST_OUT));
        setFilters(new InputFilter[]{(source, start, end, dest, left, right) -> {
            if (!isFocused() || !animateText.getAsBoolean()) return null;
            // IMEs can replace the full composing region even when only its last character changed.
            int prefix = 0;
            while (start + prefix < end && left + prefix < right
                    && source.charAt(start + prefix) == dest.charAt(left + prefix)) prefix++;
            if (prefix > 0 && left + prefix < right && Character.isLowSurrogate(dest.charAt(left + prefix))) prefix--;
            int suffix = 0;
            while (end - suffix > start + prefix && right - suffix > left + prefix
                    && source.charAt(end - suffix - 1) == dest.charAt(right - suffix - 1)) suffix++;
            if (suffix > 0 && Character.isLowSurrogate(dest.charAt(right - suffix))) suffix--;
            filteredEdit = new Edit(left, right - left, end - start, prefix, suffix);
            return null;
        }});
        addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence text, int start, int count, int after) {
                Edit filtered = filteredEdit;
                filteredEdit = null;
                preparedEdit = null;
                if (!isFocused() || !ValueAnimator.areAnimatorsEnabled() || !animateText.getAsBoolean()) return;
                Edit edit = filtered != null && filtered.start == start && filtered.removed == count && filtered.added == after
                        ? filtered : new Edit(start, count, after, 0, 0);
                preparedEdit = edit;
                int first = start + edit.prefix;
                int last = start + count - edit.suffix;
                Layout layout = getLayout();
                if (last > first && layout != null && last <= layout.getText().length()) captureDeletion(layout, first, last);
            }

            @Override
            public void onTextChanged(CharSequence text, int start, int before, int count) {
                Edit edit = preparedEdit;
                preparedEdit = null;
                if (edit == null) return;
                int first = start + edit.prefix;
                int end = start + before - edit.suffix;
                int newEnd = start + count - edit.suffix;
                if (first == end && first == newEnd) return;
                long now = SystemClock.uptimeMillis();
                ArrayList<Insertion> remaining = new ArrayList<>(inserts.size() + 1);
                for (Insertion insert : inserts) {
                    if (now - insert.time >= duration) continue;
                    if (insert.start < first) remaining.add(new Insertion(insert.start, Math.min(insert.end, first), insert.time));
                    if (insert.end > end) remaining.add(new Insertion(Math.max(insert.start, end) + newEnd - end,
                            insert.end + newEnd - end, insert.time));
                }
                if (newEnd > first) remaining.add(new Insertion(first, newEnd, now));
                inserts.clear();
                inserts.addAll(remaining);
                invalidate();
            }

            @Override
            public void afterTextChanged(Editable text) {
                stylePinyin(text);
            }
        });
    }

    private void stylePinyin(Editable text) {
        for (PinyinSeparator span : text.getSpans(0, text.length(), PinyinSeparator.class)) text.removeSpan(span);
        int start = BaseInputConnection.getComposingSpanStart(text);
        int end = BaseInputConnection.getComposingSpanEnd(text);
        if (start < 0 || end <= start || !PINYIN.matcher(text).region(start, end).matches()) return;

        int separatorWidth = Math.max(1, Math.round(getPaint().getTextSize() * .20f));
        var breaks = PINYIN_BREAK.matcher(text).region(start, end);
        while (breaks.find()) {
            text.setSpan(new PinyinSeparator(separatorWidth), breaks.start(), breaks.end(), PREEDIT_SPAN_FLAGS);
        }
    }

    private static final class PinyinSeparator extends ReplacementSpan {
        private final int width;

        PinyinSeparator(int width) {
            this.width = width;
        }

        @Override
        public int getSize(Paint paint, CharSequence text, int start, int end, Paint.FontMetricsInt metrics) {
            return width;
        }

        @Override
        public void draw(Canvas canvas, CharSequence text, int start, int end, float x, int top, int baseline, int bottom, Paint paint) {
            int clip = canvas.save();
            canvas.clipRect(x, top, x + width, bottom);
            canvas.drawText("'", x + (width - paint.measureText("'")) / 2f, baseline, paint);
            canvas.restoreToCount(clip);
        }
    }

    private float progress(long time, long now) {
        return ease.getInterpolation(Math.min(1f, (float) (now - time) / duration));
    }

    private void captureDeletion(Layout layout, int first, int last) {
        path.reset();
        layout.getSelectionPath(first, last, path);
        if (path.isEmpty()) return;
        Picture picture = new Picture();
        Canvas recording = picture.beginRecording(layout.getWidth(), layout.getHeight());
        recording.clipPath(path);
        if (getTransformationMethod() instanceof PasswordTransformationMethod) {
            Paint mask = new Paint(getPaint());
            mask.setColor(getCurrentTextColor());
            for (int index = first; index < last; index++) {
                recording.drawText("•", layout.getPrimaryHorizontal(index), layout.getLineBaseline(layout.getLineForOffset(index)), mask);
            }
        } else {
            layout.draw(recording);
        }
        picture.endRecording();
        long now = SystemClock.uptimeMillis();
        float opacity = 1f;
        for (Insertion insert : inserts) {
            if (insert.start < last && first < insert.end) opacity = Math.min(opacity, progress(insert.time, now));
        }
        deletes.add(new Deletion(picture, getCompoundPaddingLeft(), getTotalPaddingTop(), Math.round(200 * opacity), now));
    }

    @Override
    protected void onDraw(Canvas canvas) {
        long now = SystemClock.uptimeMillis();
        Layout layout = getLayout();
        boolean focused = isFocused() && hasWindowFocus() && layout != null && ValueAnimator.areAnimatorsEnabled();
        if (!focused || !animateText.getAsBoolean()) {
            inserts.clear();
            deletes.clear();
        } else {
            inserts.removeIf(edit -> now - edit.time >= duration);
            deletes.removeIf(edit -> now - edit.time >= duration);
        }

        boolean caretEnabled = focused && animateCaret.getAsBoolean();
        boolean drawCursor = caretEnabled && updateCursor(layout, now);
        if (drawCursor != movingCursor) {
            movingCursor = drawCursor;
            setCursorVisible(!movingCursor);
        }
        if (!caretEnabled) cursorReady = false;

        int left = getScrollX();
        int top = getScrollY();
        int right = left + getWidth();
        int bottom = top + getHeight();
        int layer = inserts.isEmpty() ? 0 : canvas.saveLayer(left, top, right, bottom, null);
        super.onDraw(canvas);
        if (!inserts.isEmpty()) {
            canvas.translate(getCompoundPaddingLeft(), getTotalPaddingTop());
            for (Insertion edit : inserts) {
                path.reset();
                layout.getSelectionPath(edit.start, edit.end, path);
                erase.setAlpha(Math.round(255 * (1f - progress(edit.time, now))));
                canvas.drawPath(path, erase);
            }
            canvas.restoreToCount(layer);
        }

        int clip = canvas.save();
        canvas.clipRect(left + getCompoundPaddingLeft(), top + getExtendedPaddingTop(),
                right - getCompoundPaddingRight(), bottom - getExtendedPaddingBottom());
        for (Deletion edit : deletes) {
            int ghost = canvas.saveLayerAlpha(left, top, right, bottom, Math.round(edit.alpha * (1f - progress(edit.time, now))));
            canvas.translate(edit.left, edit.top);
            canvas.drawPicture(edit.picture);
            canvas.restoreToCount(ghost);
        }
        if (drawCursor) {
            float p = progress(cursorStart, now);
            float x = fromX + (toX - fromX) * p;
            float y = fromTop + (toTop - fromTop) * p;
            float height = fromBottom + (toBottom - fromBottom) * p;
            float halfWidth = getResources().getDisplayMetrics().density;
            cursorPaint.setColor(getCurrentTextColor());
            canvas.drawRoundRect(x - halfWidth, y, x + halfWidth, height, halfWidth, halfWidth, cursorPaint);
        }
        canvas.restoreToCount(clip);
        if (drawCursor || !inserts.isEmpty() || !deletes.isEmpty()) postInvalidateOnAnimation();
    }

    private boolean updateCursor(Layout layout, long now) {
        int point = getSelectionStart();
        if (point < 0 || point != getSelectionEnd()) {
            cursorReady = false;
            return false;
        }
        int line = layout.getLineForOffset(point);
        float x = getCompoundPaddingLeft() + layout.getPrimaryHorizontal(point);
        float top = getTotalPaddingTop() + layout.getLineTop(line);
        float bottom = getTotalPaddingTop() + layout.getLineBottom(line);
        if (!cursorReady) {
            cursorReady = true;
            toX = x;
            toTop = top;
            toBottom = bottom;
            cursorStart = now - duration;
        } else if (x != toX || top != toTop || bottom != toBottom) {
            float p = movingCursor ? progress(cursorStart, now) : 1f;
            fromX += (toX - fromX) * p;
            fromTop += (toTop - fromTop) * p;
            fromBottom += (toBottom - fromBottom) * p;
            toX = x;
            toTop = top;
            toBottom = bottom;
            cursorStart = now;
        }
        return now - cursorStart < duration;
    }

    @Override
    protected void onFocusChanged(boolean focused, int direction, Rect previous) {
        super.onFocusChanged(focused, direction, previous);
        if (!focused && movingCursor) {
            movingCursor = false;
            setCursorVisible(true);
        }
        cursorReady = false;
    }
}
