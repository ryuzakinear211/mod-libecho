//Please don't replace listeners with lambda!

package com.android.support;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.AlertDialog;
import android.app.Service;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.graphics.PorterDuff;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.text.Html;
import android.text.InputFilter;
import android.text.InputType;
import android.text.TextUtils;
import android.text.method.DigitsKeyListener;
import android.util.Base64;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.webkit.WebView;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.RelativeLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import android.os.Looper;
import java.io.File;
import java.io.FileOutputStream;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.Arrays;
import java.util.LinkedList;
import java.util.List;

import static android.view.ViewGroup.LayoutParams.WRAP_CONTENT;
import static android.view.ViewGroup.LayoutParams.MATCH_PARENT;
import static android.widget.RelativeLayout.ALIGN_PARENT_LEFT;
import static android.widget.RelativeLayout.ALIGN_PARENT_RIGHT;

import org.xml.sax.ErrorHandler;

public class Menu {
    //********** Here you can easly change the menu appearance **********//

    //region Variable
    public static final String TAG = "Mod_Menu"; //Tag for logcat

    int TEXT_COLOR = Color.parseColor("#82CAFD");
    int TEXT_COLOR_2 = Color.parseColor("#FFFFFF");
    int BTN_COLOR = Color.parseColor("#1C262D");
    int MENU_BG_COLOR = Color.parseColor("#EE1C2A35"); //#AARRGGBB
    int MENU_FEATURE_BG_COLOR = Color.parseColor("#DD141C22"); //#AARRGGBB
    int MENU_WIDTH = 290;
    int MENU_HEIGHT = 210;
    int POS_X = 0;
    int POS_Y = 100;

    float MENU_CORNER = 4f;
    int ICON_SIZE = 45; //Change both width and height of image
    float ICON_ALPHA = 0.7f; //Transparent
    int ToggleON = Color.GREEN;
    int ToggleOFF = Color.RED;
    int BtnON = Color.parseColor("#1b5e20");
    int BtnOFF = Color.parseColor("#7f0000");
    int CategoryBG = Color.parseColor("#2F3D4C");
    int SeekBarColor = Color.parseColor("#80CBC4");
    int SeekBarProgressColor = Color.parseColor("#80CBC4");
    int CheckBoxColor = Color.parseColor("#80CBC4");
    int RadioColor = Color.parseColor("#FFFFFF");
    String NumberTxtColor = "#41c300";
    //********************************************************************//

    RelativeLayout mCollapsed, mRootContainer;
    LinearLayout mExpanded, mods, mSettings, mCollapse;
    LinearLayout.LayoutParams scrlLLExpanded, scrlLL;
    WindowManager mWindowManager;
    WindowManager.LayoutParams vmParams;
    ImageView startimage;
    FrameLayout rootFrame;
    ScrollView scrollView;
    boolean stopChecking, overlayRequired;
    Context getContext;

    // HUD Status Panel Components
    FrameLayout hudRootFrame;
    WindowManager.LayoutParams hudParams;
    LinearLayout hudPanelContainer;
    LinearLayout hudContentContainer;
    TextView hudHeaderTitle;
    TextView hudStatusBadge;
    TextView hudSummaryRow;
    TextView hudTeamHeader;
    TextView hudTeamList;
    TextView hudEnemyHeader;
    TextView hudEnemyList;
    TextView hudCompactText;
    ScrollView hudRosterScroll;
    boolean isHudCompact = false;
    boolean isHudEnabled = false; // OFF by default
    Handler hudUpdateHandler;
    Runnable hudUpdateRunnable;

    //initialize methods from the native library
    native void Init(Context context, TextView title, TextView subTitle);

    native String Icon();

    native String IconWebViewData();

    native String[] GetFeatureList();

    native String[] SettingsList();

    native boolean IsGameLibLoaded();

    // HUD Status Panel & Full Bright native methods
    native String GetHudStatus();

    native String GetHudRoster();

    native int GetPlayerCount();

    native int GetBotCount();

    native int GetAliveCount();

    native boolean IsInMatch();

    native boolean IsLocalPlayerAlive();

    native boolean IsFullBright();

    //Here we write the code for our Menu
    // Reference: https://www.androidhive.info/2016/11/android-floating-widget-like-facebook-chat-head/
    public Menu(Context context) {

        getContext = context;
        Preferences.context = context;
        rootFrame = new FrameLayout(context); // Global markup
        rootFrame.setOnTouchListener(onTouchListener());
        mRootContainer = new RelativeLayout(context); // Markup on which two markups of the icon and the menu itself will be placed
        mCollapsed = new RelativeLayout(context); // Markup of the icon (when the menu is minimized)
        mCollapsed.setVisibility(View.VISIBLE);
        mCollapsed.setAlpha(ICON_ALPHA);

        //********** The box of the mod menu **********
        mExpanded = new LinearLayout(context); // Menu markup (when the menu is expanded)
        mExpanded.setVisibility(View.GONE);
        mExpanded.setBackgroundColor(MENU_BG_COLOR);
        mExpanded.setOrientation(LinearLayout.VERTICAL);
        // mExpanded.setPadding(1, 1, 1, 1); //So borders would be visible
        mExpanded.setLayoutParams(new LinearLayout.LayoutParams(dp(MENU_WIDTH), WRAP_CONTENT));
        GradientDrawable gdMenuBody = new GradientDrawable();
        gdMenuBody.setCornerRadius(MENU_CORNER); //Set corner
        gdMenuBody.setColor(MENU_BG_COLOR); //Set background color
        gdMenuBody.setStroke(1, Color.parseColor("#32cb00")); //Set border
        //mExpanded.setBackground(gdMenuBody); //Apply GradientDrawable to it

        //********** The icon to open mod menu **********
        startimage = new ImageView(context);
        startimage.setLayoutParams(new RelativeLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT));
        int applyDimension = (int) TypedValue.applyDimension(1, ICON_SIZE, context.getResources().getDisplayMetrics()); //Icon size
        startimage.getLayoutParams().height = applyDimension;
        startimage.getLayoutParams().width = applyDimension;
        //startimage.requestLayout();
        startimage.setScaleType(ImageView.ScaleType.FIT_XY);
        byte[] decode = Base64.decode(Icon(), 0);
        startimage.setImageBitmap(BitmapFactory.decodeByteArray(decode, 0, decode.length));
        ((ViewGroup.MarginLayoutParams) startimage.getLayoutParams()).topMargin = convertDipToPixels(10);
        //Initialize event handlers for buttons, etc.
        startimage.setOnTouchListener(onTouchListener());
        startimage.setOnClickListener(new View.OnClickListener() {
            public void onClick(View view) {
                mCollapsed.setVisibility(View.GONE);
                mExpanded.setVisibility(View.VISIBLE);
            }
        });

        //********** The icon in Webview to open mod menu **********
        WebView wView = new WebView(context); //Icon size width=\"50\" height=\"50\"
        wView.setLayoutParams(new RelativeLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT));
        int applyDimension2 = (int) TypedValue.applyDimension(1, ICON_SIZE, context.getResources().getDisplayMetrics()); //Icon size
        wView.getLayoutParams().height = applyDimension2;
        wView.getLayoutParams().width = applyDimension2;
        wView.loadData("<html>" +
                "<head></head>" +
                "<body style=\"margin: 0; padding: 0\">" +
                "<img src=\"" + IconWebViewData() + "\" width=\"" + ICON_SIZE + "\" height=\"" + ICON_SIZE + "\" >" +
                "</body>" +
                "</html>", "text/html", "utf-8");
        wView.setBackgroundColor(0x00000000); //Transparent
        wView.setAlpha(ICON_ALPHA);
        try {
            wView.getSettings().getClass().getMethod("setAppCacheEnabled", boolean.class).invoke(wView.getSettings(), true);
        } catch (Exception ignored) {
        }
        wView.setOnTouchListener(onTouchListener());

        //********** Settings icon **********
        TextView settings = new TextView(context); //Android 5 can't show ⚙, instead show other icon instead
        settings.setText(Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.M ? "⚙" : "\uD83D\uDD27");
        settings.setTextColor(TEXT_COLOR);
        settings.setTypeface(Typeface.DEFAULT_BOLD);
        settings.setTextSize(20.0f);
        RelativeLayout.LayoutParams rlsettings = new RelativeLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT);
        rlsettings.addRule(ALIGN_PARENT_RIGHT);
        settings.setLayoutParams(rlsettings);
        settings.setOnClickListener(new View.OnClickListener() {
            boolean settingsOpen;

            @Override
            public void onClick(View v) {
                try {
                    settingsOpen = !settingsOpen;
                    if (settingsOpen) {
                        scrollView.removeView(mods);
                        scrollView.addView(mSettings);
                        scrollView.scrollTo(0, 0);
                    } else {
                        scrollView.removeView(mSettings);
                        scrollView.addView(mods);
                    }
                } catch (IllegalStateException e) {
                }
            }
        });

        //********** Settings **********
        mSettings = new LinearLayout(context);
        mSettings.setOrientation(LinearLayout.VERTICAL);
        featureList(SettingsList(), mSettings);

        //********** Title **********
        RelativeLayout titleText = new RelativeLayout(context);
        titleText.setPadding(10, 5, 10, 5);
        titleText.setVerticalGravity(16);

        TextView title = new TextView(context);
        title.setTextColor(TEXT_COLOR);
        title.setTextSize(18.0f);
        title.setGravity(Gravity.CENTER);
        RelativeLayout.LayoutParams rl = new RelativeLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT);
        rl.addRule(RelativeLayout.CENTER_HORIZONTAL);
        title.setLayoutParams(rl);

        //********** Sub title **********
        TextView subTitle = new TextView(context);
        subTitle.setEllipsize(TextUtils.TruncateAt.MARQUEE);
        subTitle.setMarqueeRepeatLimit(-1);
        subTitle.setSingleLine(true);
        subTitle.setSelected(true);
        subTitle.setTextColor(TEXT_COLOR);
        subTitle.setTextSize(10.0f);
        subTitle.setGravity(Gravity.CENTER);
        subTitle.setPadding(0, 0, 0, 5);

        //********** Mod menu feature list **********
        scrollView = new ScrollView(context);
        //Auto size. To set size manually, change the width and height example 500, 500
        scrlLL = new LinearLayout.LayoutParams(MATCH_PARENT, dp(MENU_HEIGHT));
        scrlLLExpanded = new LinearLayout.LayoutParams(mExpanded.getLayoutParams());
        scrlLLExpanded.weight = 1.0f;
        scrollView.setLayoutParams(Preferences.isExpanded ? scrlLLExpanded : scrlLL);
        scrollView.setBackgroundColor(MENU_FEATURE_BG_COLOR);
        mods = new LinearLayout(context);
        mods.setOrientation(LinearLayout.VERTICAL);

        //********** RelativeLayout for buttons **********
        RelativeLayout relativeLayout = new RelativeLayout(context);
        relativeLayout.setPadding(10, 3, 10, 3);
        relativeLayout.setVerticalGravity(Gravity.CENTER);

        //**********  Hide/Kill button **********
        RelativeLayout.LayoutParams lParamsHideBtn = new RelativeLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT);
        lParamsHideBtn.addRule(ALIGN_PARENT_LEFT);

        Button hideBtn = new Button(context);
        hideBtn.setLayoutParams(lParamsHideBtn);
        hideBtn.setBackgroundColor(Color.TRANSPARENT);
        hideBtn.setText("HIDE/KILL (Hold)");
        hideBtn.setTextColor(TEXT_COLOR);
        hideBtn.setOnClickListener(new View.OnClickListener() {
            public void onClick(View view) {
                mCollapsed.setVisibility(View.VISIBLE);
                mCollapsed.setAlpha(0);
                mExpanded.setVisibility(View.GONE);
                Toast.makeText(view.getContext(), "Icon hidden. Remember the hidden icon position", Toast.LENGTH_LONG).show();
            }
        });
        hideBtn.setOnLongClickListener(new View.OnLongClickListener() {
            public boolean onLongClick(View view) {
                Toast.makeText(view.getContext(), "Menu killed", Toast.LENGTH_LONG).show();
                rootFrame.removeView(mRootContainer);
                mWindowManager.removeView(rootFrame);
                return false;
            }
        });

        //********** Close button **********
        RelativeLayout.LayoutParams lParamsCloseBtn = new RelativeLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT);
        lParamsCloseBtn.addRule(ALIGN_PARENT_RIGHT);

        Button closeBtn = new Button(context);
        closeBtn.setLayoutParams(lParamsCloseBtn);
        closeBtn.setBackgroundColor(Color.TRANSPARENT);
        closeBtn.setText("MINIMIZE");
        closeBtn.setTextColor(TEXT_COLOR);
        closeBtn.setOnClickListener(new View.OnClickListener() {
            public void onClick(View view) {
                mCollapsed.setVisibility(View.VISIBLE);
                mCollapsed.setAlpha(ICON_ALPHA);
                mExpanded.setVisibility(View.GONE);
            }
        });

        //********** Adding view components **********
        mRootContainer.addView(mCollapsed);
        mRootContainer.addView(mExpanded);
        if (IconWebViewData() != null) {
            mCollapsed.addView(wView);
        } else {
            mCollapsed.addView(startimage);
        }
        titleText.addView(title);
        titleText.addView(settings);
        mExpanded.addView(titleText);
        mExpanded.addView(subTitle);
        scrollView.addView(mods);
        mExpanded.addView(scrollView);
        relativeLayout.addView(hideBtn);
        relativeLayout.addView(closeBtn);
        mExpanded.addView(relativeLayout);

        Init(context, title, subTitle);
        initHudStatusPanel(context);
    }

    public void ShowMenu() {
        rootFrame.addView(mRootContainer);

        final Handler handler = new Handler();
        handler.postDelayed(new Runnable() {
            boolean viewLoaded = false;

            @Override
            public void run() {
                //If the save preferences is enabled, it will check if game lib is loaded before starting menu
                //Comment the if-else code out except startService if you want to run the app and test preferences
                if (Preferences.loadPref && !IsGameLibLoaded() && !stopChecking) {
                    if (!viewLoaded) {
                        Category(mods, "Save preferences was been enabled. Waiting for game lib to be loaded...\n\nForce load menu may not apply mods instantly. You would need to reactivate them again");
                        Button(mods, -100, "Force load menu");
                        viewLoaded = true;
                    }
                    handler.postDelayed(this, 600);
                } else {
                    mods.removeAllViews();
                    featureList(GetFeatureList(), mods);
                }
            }
        }, 500);
    }

    @SuppressLint("WrongConstant")
    public void SetWindowManagerWindowService() {
        //Variable to check later if the phone supports Draw over other apps permission
        int iparams = Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.O ? 2038 : 2002;
        vmParams = new WindowManager.LayoutParams(WRAP_CONTENT, WRAP_CONTENT, iparams, 8, -3);
        //params = new WindowManager.LayoutParams(WindowManager.LayoutParams.LAST_APPLICATION_WINDOW, 8, -3);
        vmParams.gravity = 51;
        vmParams.x = POS_X;
        vmParams.y = POS_Y;

        mWindowManager = (WindowManager) getContext.getSystemService(getContext.WINDOW_SERVICE);
        mWindowManager.addView(rootFrame, vmParams);

        if (hudRootFrame != null && hudParams != null) {
            try {
                mWindowManager.addView(hudRootFrame, hudParams);
                if (hudUpdateHandler != null && hudUpdateRunnable != null) {
                    hudUpdateHandler.post(hudUpdateRunnable);
                }
                logToFile("HUD Status Panel attached to WindowService successfully");
            } catch (Exception e) {
                Log.e(TAG, "Failed to add HUD frame to WindowService", e);
                logToFile("Failed to add HUD frame: " + e.getMessage());
            }
        }

        overlayRequired = true;
    }

    @SuppressLint("WrongConstant")
    public void SetWindowManagerActivity() {
        vmParams = new WindowManager.LayoutParams(
                WindowManager.LayoutParams.WRAP_CONTENT,
                WindowManager.LayoutParams.WRAP_CONTENT,
                POS_X,//initialX
                POS_Y,//initialy
                WindowManager.LayoutParams.TYPE_APPLICATION,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                        WindowManager.LayoutParams.FLAG_LAYOUT_IN_OVERSCAN |
                        WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN |
                        WindowManager.LayoutParams.FLAG_SPLIT_TOUCH,
                PixelFormat.TRANSPARENT
        );
        vmParams.gravity = 51;
        vmParams.x = POS_X;
        vmParams.y = POS_Y;

        mWindowManager = ((Activity) getContext).getWindowManager();
        mWindowManager.addView(rootFrame, vmParams);

        if (hudRootFrame != null && hudParams != null) {
            try {
                mWindowManager.addView(hudRootFrame, hudParams);
                if (hudUpdateHandler != null && hudUpdateRunnable != null) {
                    hudUpdateHandler.post(hudUpdateRunnable);
                }
                logToFile("HUD Status Panel attached to Activity successfully");
            } catch (Exception e) {
                Log.e(TAG, "Failed to add HUD frame to Activity", e);
                logToFile("Failed to add HUD frame: " + e.getMessage());
            }
        }
    }

    private View.OnTouchListener onTouchListener() {
        return new View.OnTouchListener() {
            final View collapsedView = mCollapsed;
            final View expandedView = mExpanded;
            private float initialTouchX, initialTouchY;
            private int initialX, initialY;

            public boolean onTouch(View view, MotionEvent motionEvent) {
                switch (motionEvent.getAction()) {
                    case MotionEvent.ACTION_DOWN:
                        initialX = vmParams.x;
                        initialY = vmParams.y;
                        initialTouchX = motionEvent.getRawX();
                        initialTouchY = motionEvent.getRawY();
                        return true;
                    case MotionEvent.ACTION_UP:
                        int rawX = (int) (motionEvent.getRawX() - initialTouchX);
                        int rawY = (int) (motionEvent.getRawY() - initialTouchY);
                        mExpanded.setAlpha(1f);
                        mCollapsed.setAlpha(1f);
                        //The check for Xdiff <10 && YDiff< 10 because sometime elements moves a little while clicking.
                        //So that is click event.
                        if (rawX < 10 && rawY < 10 && isViewCollapsed()) {
                            //When user clicks on the image view of the collapsed layout,
                            //visibility of the collapsed layout will be changed to "View.GONE"
                            //and expanded view will become visible.
                            try {
                                collapsedView.setVisibility(View.GONE);
                                expandedView.setVisibility(View.VISIBLE);
                            } catch (NullPointerException e) {

                            }
                        }
                        return true;
                    case MotionEvent.ACTION_MOVE:
                        mExpanded.setAlpha(0.5f);
                        mCollapsed.setAlpha(0.5f);
                        //Calculate the X and Y coordinates of the view.
                        vmParams.x = initialX + ((int) (motionEvent.getRawX() - initialTouchX));
                        vmParams.y = initialY + ((int) (motionEvent.getRawY() - initialTouchY));
                        //Update the layout with new X & Y coordinate
                        mWindowManager.updateViewLayout(rootFrame, vmParams);
                        return true;
                    default:
                        return false;
                }
            }
        };
    }

    private void featureList(String[] listFT, LinearLayout linearLayout) {
        //Currently looks messy right now. Let me know if you have improvements
        int featNum, subFeat = 0;
        LinearLayout llBak = linearLayout;

        for (int i = 0; i < listFT.length; i++) {
            boolean switchedOn = false;
            //Log.i("featureList", listFT[i]);
            String feature = listFT[i];
            if (feature.contains("_True")) {
                switchedOn = true;
                feature = feature.replaceFirst("_True", "");
            }

            linearLayout = llBak;
            if (feature.contains("CollapseAdd_")) {
                //if (collapse != null)
                linearLayout = mCollapse;
                feature = feature.replaceFirst("CollapseAdd_", "");
            }
            String[] str = feature.split("_");

            //Assign feature number
            if (TextUtils.isDigitsOnly(str[0]) || str[0].matches("-[0-9]*")) {
                featNum = Integer.parseInt(str[0]);
                feature = feature.replaceFirst(str[0] + "_", "");
                subFeat++;
            } else {
                //Subtract feature number. We don't want to count ButtonLink, Category, RichTextView and RichWebView
                featNum = i - subFeat;
            }
            String[] strSplit = feature.split("_");
            switch (strSplit[0]) {
                case "Toggle":
                    Switch(linearLayout, featNum, strSplit[1], switchedOn);
                    break;
                case "SeekBar":
                    SeekBar(linearLayout, featNum, strSplit[1], Integer.parseInt(strSplit[2]), Integer.parseInt(strSplit[3]));
                    break;
                case "Button":
                    Button(linearLayout, featNum, strSplit[1]);
                    break;
                case "ButtonOnOff":
                    ButtonOnOff(linearLayout, featNum, strSplit[1], switchedOn);
                    break;
                case "Spinner":
                    TextView(linearLayout, strSplit[1]);
                    Spinner(linearLayout, featNum, strSplit[1], strSplit[2]);
                    break;
                case "InputText":
                    InputText(linearLayout, featNum, strSplit[1]);
                    break;
                case "InputValue":
                    if (strSplit.length == 3)
                        InputNum(linearLayout, featNum, strSplit[2], Integer.parseInt(strSplit[1]));
                    if (strSplit.length == 2)
                        InputNum(linearLayout, featNum, strSplit[1], 0);
                    break;
                case "CheckBox":
                    CheckBox(linearLayout, featNum, strSplit[1], switchedOn);
                    break;
                case "RadioButton":
                    RadioButton(linearLayout, featNum, strSplit[1], strSplit[2]);
                    break;
                case "Collapse":
                    Collapse(linearLayout, strSplit[1], switchedOn);
                    subFeat++;
                    break;
                case "ButtonLink":
                    subFeat++;
                    ButtonLink(linearLayout, strSplit[1], strSplit[2]);
                    break;
                case "Category":
                    subFeat++;
                    Category(linearLayout, strSplit[1]);
                    break;
                case "RichTextView":
                    subFeat++;
                    TextView(linearLayout, strSplit[1]);
                    break;
                case "RichWebView":
                    subFeat++;
                    WebTextView(linearLayout, strSplit[1]);
                    break;
            }
        }
    }

    private void Switch(LinearLayout linLayout, final int featNum, final String featName, boolean swiOn) {
        final Switch switchR = new Switch(getContext);
        ColorStateList buttonStates = new ColorStateList(
                new int[][]{
                        new int[]{-android.R.attr.state_enabled},
                        new int[]{android.R.attr.state_checked},
                        new int[]{}
                },
                new int[]{
                        Color.BLUE,
                        ToggleON, // ON
                        ToggleOFF // OFF
                }
        );
        //Set colors of the switch. Comment out if you don't like it
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            try {
                switchR.getThumbDrawable().setTintList(buttonStates);
                switchR.getTrackDrawable().setTintList(buttonStates);
            } catch (NullPointerException ex) {
                Log.d(TAG, String.valueOf(ex));
            }
        }
        switchR.setText(featName);
        switchR.setTextColor(TEXT_COLOR_2);
        switchR.setPadding(10, 5, 0, 5);
        switchR.setChecked(Preferences.loadPrefBool(featName, featNum, swiOn));
        switchR.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            public void onCheckedChanged(CompoundButton compoundButton, boolean bool) {
                Preferences.changeFeatureBool(featName, featNum, bool);
                switch (featNum) {
                    case -1: //Save perferences
                        Preferences.with(switchR.getContext()).writeBoolean(-1, bool);
                        if (bool == false)
                            Preferences.with(switchR.getContext()).clear(); //Clear perferences if switched off
                        break;
                    case -3:
                        Preferences.isExpanded = bool;
                        scrollView.setLayoutParams(bool ? scrlLLExpanded : scrlLL);
                        break;
                    case 0: // HUD Status Panel toggle (OFF by default)
                        isHudEnabled = bool;
                        if (hudRootFrame != null) {
                            if (bool && getContext != null) {
                                int screenWidth = getContext.getResources().getDisplayMetrics().widthPixels;
                                int panelW = dp(240);
                                hudParams.x = Math.max(0, (screenWidth - panelW) / 2);
                                hudParams.y = dp(16);
                                if (mWindowManager != null) {
                                    try {
                                        mWindowManager.updateViewLayout(hudRootFrame, hudParams);
                                    } catch (Exception ignored) {}
                                }
                            }
                            hudRootFrame.setVisibility(bool ? View.VISIBLE : View.GONE);
                        }
                        logToFile("HUD Status Panel toggled: " + (bool ? "VISIBLE (TOP-CENTER)" : "HIDDEN"));
                        break;
                    case 1: // Full Bright toggle
                        logToFile("Full Bright toggled: " + (bool ? "ON" : "OFF"));
                        break;
                    case 2: // Camera Zoom Out toggle
                        logToFile("Camera Zoom Out toggled: " + (bool ? "ON" : "OFF"));
                        break;
                    case 4: // Max Weapon Range toggle
                        logToFile("Max Weapon Range toggled: " + (bool ? "ON" : "OFF"));
                        break;
                    case 5: // Wide Laser Area toggle
                        logToFile("Wide Laser Area toggled: " + (bool ? "ON" : "OFF"));
                        break;
                    case 6: // Show Position toggle
                        logToFile("Show Position toggled: " + (bool ? "ON" : "OFF"));
                        break;
                }
            }
        });

        linLayout.addView(switchR);
    }

    private void SeekBar(LinearLayout linLayout, final int featNum, final String featName, final int min, int max) {
        int loadedProg = Preferences.loadPrefInt(featName, featNum);
        LinearLayout linearLayout = new LinearLayout(getContext);
        linearLayout.setPadding(10, 5, 0, 5);
        linearLayout.setOrientation(LinearLayout.VERTICAL);
        linearLayout.setGravity(Gravity.CENTER);

        final TextView textView = new TextView(getContext);
        textView.setText(Html.fromHtml(featName + ": <font color='" + NumberTxtColor + "'>" + ((loadedProg == 0) ? min : loadedProg)));
        textView.setTextColor(TEXT_COLOR_2);

        SeekBar seekBar = new SeekBar(getContext);
        seekBar.setPadding(25, 10, 35, 10);
        seekBar.setMax(max);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
            seekBar.setMin(min); //setMin for Oreo and above
        seekBar.setProgress((loadedProg == 0) ? min : loadedProg);
        seekBar.getThumb().setColorFilter(SeekBarColor, PorterDuff.Mode.SRC_ATOP);
        seekBar.getProgressDrawable().setColorFilter(SeekBarProgressColor, PorterDuff.Mode.SRC_ATOP);
        seekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onStartTrackingTouch(SeekBar seekBar) {
            }

            public void onStopTrackingTouch(SeekBar seekBar) {
            }

            public void onProgressChanged(SeekBar seekBar, int i, boolean z) {
                //if progress is greater than minimum, don't go below. Else, set progress
                int finalVal = i < min ? min : i;
                seekBar.setProgress(finalVal);
                Preferences.changeFeatureInt(featName, featNum, finalVal);
                textView.setText(Html.fromHtml(featName + ": <font color='" + NumberTxtColor + "'>" + finalVal));
                if (featNum == 3) {
                    logToFile("Camera Zoom Level adjusted: " + (finalVal / 10.0f) + "x");
                }
            }
        });
        linearLayout.addView(textView);
        linearLayout.addView(seekBar);

        linLayout.addView(linearLayout);
    }

    private void Button(LinearLayout linLayout, final int featNum, final String featName) {
        final Button button = new Button(getContext);
        LinearLayout.LayoutParams layoutParams = new LinearLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT);
        layoutParams.setMargins(7, 5, 7, 5);
        button.setLayoutParams(layoutParams);
        button.setTextColor(TEXT_COLOR_2);
        button.setAllCaps(false); //Disable caps to support html
        button.setText(Html.fromHtml(featName));
        button.setBackgroundColor(BTN_COLOR);
        button.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                switch (featNum) {

                    case -6:
                        scrollView.removeView(mSettings);
                        scrollView.addView(mods);
                        break;
                    case -100:
                        stopChecking = true;
                        break;
                }
                Preferences.changeFeatureInt(featName, featNum, 0);
            }
        });

        linLayout.addView(button);
    }

    private void ButtonLink(LinearLayout linLayout, final String featName, final String url) {
        final Button button = new Button(getContext);
        LinearLayout.LayoutParams layoutParams = new LinearLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT);
        layoutParams.setMargins(7, 5, 7, 5);
        button.setLayoutParams(layoutParams);
        button.setAllCaps(false); //Disable caps to support html
        button.setTextColor(TEXT_COLOR_2);
        button.setText(Html.fromHtml(featName));
        button.setBackgroundColor(BTN_COLOR);
        button.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                Intent intent = new Intent(Intent.ACTION_VIEW);
                intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                intent.setData(Uri.parse(url));
                getContext.startActivity(intent);
            }
        });
        linLayout.addView(button);
    }

    private void ButtonOnOff(LinearLayout linLayout, final int featNum, String featName, boolean switchedOn) {
        final Button button = new Button(getContext);
        LinearLayout.LayoutParams layoutParams = new LinearLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT);
        layoutParams.setMargins(7, 5, 7, 5);
        button.setLayoutParams(layoutParams);
        button.setTextColor(TEXT_COLOR_2);
        button.setAllCaps(false); //Disable caps to support html

        final String finalfeatName = featName.replace("OnOff_", "");
        boolean isOn = Preferences.loadPrefBool(featName, featNum, switchedOn);
        if (isOn) {
            button.setText(Html.fromHtml(finalfeatName + ": ON"));
            button.setBackgroundColor(BtnON);
            isOn = false;
        } else {
            button.setText(Html.fromHtml(finalfeatName + ": OFF"));
            button.setBackgroundColor(BtnOFF);
            isOn = true;
        }
        final boolean finalIsOn = isOn;
        button.setOnClickListener(new View.OnClickListener() {
            boolean isOn = finalIsOn;

            public void onClick(View v) {
                Preferences.changeFeatureBool(finalfeatName, featNum, isOn);
                //Log.d(TAG, finalfeatName + " " + featNum + " " + isActive2);
                if (isOn) {
                    button.setText(Html.fromHtml(finalfeatName + ": ON"));
                    button.setBackgroundColor(BtnON);
                    isOn = false;
                } else {
                    button.setText(Html.fromHtml(finalfeatName + ": OFF"));
                    button.setBackgroundColor(BtnOFF);
                    isOn = true;
                }
            }
        });
        linLayout.addView(button);
    }

    private void Spinner(LinearLayout linLayout, final int featNum, final String featName, final String list) {
        Log.d(TAG, "spinner " + featNum + " " + featName + " " + list);
        final List<String> lists = new LinkedList<>(Arrays.asList(list.split(",")));

        // Create another LinearLayout as a workaround to use it as a background
        // to keep the down arrow symbol. No arrow symbol if setBackgroundColor set
        LinearLayout linearLayout2 = new LinearLayout(getContext);
        LinearLayout.LayoutParams layoutParams2 = new LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT);
        layoutParams2.setMargins(7, 2, 7, 2);
        linearLayout2.setOrientation(LinearLayout.VERTICAL);
        linearLayout2.setBackgroundColor(BTN_COLOR);
        linearLayout2.setLayoutParams(layoutParams2);

        final Spinner spinner = new Spinner(getContext, Spinner.MODE_DROPDOWN);
        spinner.setLayoutParams(layoutParams2);
        spinner.getBackground().setColorFilter(1, PorterDuff.Mode.SRC_ATOP); //trick to show white down arrow color
        //Creating the ArrayAdapter instance having the list
        ArrayAdapter aa = new ArrayAdapter(getContext, android.R.layout.simple_spinner_dropdown_item, lists);
        aa.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        //Setting the ArrayAdapter data on the Spinner'
        spinner.setAdapter(aa);
        spinner.setSelection(Preferences.loadPrefInt(featName, featNum));
        spinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parentView, View selectedItemView, int position, long id) {
                Preferences.changeFeatureInt(spinner.getSelectedItem().toString(), featNum, position);
                ((TextView) parentView.getChildAt(0)).setTextColor(TEXT_COLOR_2);
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {
            }
        });
        linearLayout2.addView(spinner);
        linLayout.addView(linearLayout2);
    }

    private void InputNum(LinearLayout linLayout, final int featNum, final String featName, final int maxValue) {
        LinearLayout linearLayout = new LinearLayout(getContext);
        LinearLayout.LayoutParams layoutParams = new LinearLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT);
        layoutParams.setMargins(7, 5, 7, 5);

        final Button button = new Button(getContext);
        int num = Preferences.loadPrefInt(featName, featNum);
        button.setText(Html.fromHtml(featName + ": <font color='" + NumberTxtColor + "'>" + ((num == 0) ? 1 : num) + "</font>"));
        button.setAllCaps(false);
        button.setLayoutParams(layoutParams);
        button.setBackgroundColor(BTN_COLOR);
        button.setTextColor(TEXT_COLOR_2);
        button.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                AlertDialog.Builder alertName = new AlertDialog.Builder(getContext);
                final EditText editText = new EditText(getContext);
                if (maxValue != 0)
                    editText.setHint("Max value: " + maxValue);
                editText.setInputType(InputType.TYPE_CLASS_NUMBER);
                editText.setKeyListener(DigitsKeyListener.getInstance("0123456789-"));
                InputFilter[] FilterArray = new InputFilter[1];
                FilterArray[0] = new InputFilter.LengthFilter(10);
                editText.setFilters(FilterArray);
                editText.setOnFocusChangeListener(new View.OnFocusChangeListener() {
                    @Override
                    public void onFocusChange(View v, boolean hasFocus) {
                        InputMethodManager imm = (InputMethodManager) getContext.getSystemService(getContext.INPUT_METHOD_SERVICE);
                        if (hasFocus) {
                            imm.toggleSoftInput(InputMethodManager.SHOW_FORCED, InputMethodManager.HIDE_IMPLICIT_ONLY);
                        } else {
                            imm.toggleSoftInput(InputMethodManager.HIDE_IMPLICIT_ONLY, 0);
                        }
                    }
                });
                editText.requestFocus();

                alertName.setTitle("Input number");
                alertName.setView(editText);
                LinearLayout layoutName = new LinearLayout(getContext);
                layoutName.setOrientation(LinearLayout.VERTICAL);
                layoutName.addView(editText); // displays the user input bar
                alertName.setView(layoutName);

                alertName.setPositiveButton("OK", new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface dialog, int whichButton) {
                        int num;
                        try {
                            num = Integer.parseInt(TextUtils.isEmpty(editText.getText().toString()) ? "0" : editText.getText().toString());
                            if (maxValue != 0 && num >= maxValue)
                                num = maxValue;
                        } catch (NumberFormatException ex) {
                            if (maxValue != 0)
                                num = maxValue;
                            else
                                num = 2147483640;
                        }

                        button.setText(Html.fromHtml(featName + ": <font color='" + NumberTxtColor + "'>" + num + "</font>"));
                        Preferences.changeFeatureInt(featName, featNum, num);

                        editText.setFocusable(false);
                    }
                });

                alertName.setNegativeButton("Cancel", new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface dialog, int whichButton) {
                        // dialog.cancel(); // closes dialog
                        InputMethodManager imm = (InputMethodManager) getContext.getSystemService(getContext.INPUT_METHOD_SERVICE);
                        imm.toggleSoftInput(InputMethodManager.HIDE_IMPLICIT_ONLY, 0);
                    }
                });

                if (overlayRequired) {
                    AlertDialog dialog = alertName.create(); // display the dialog
                    dialog.getWindow().setType(Build.VERSION.SDK_INT >= 26 ? 2038 : 2002);
                    dialog.show();
                } else {
                    alertName.show();
                }
            }
        });

        linearLayout.addView(button);
        linLayout.addView(linearLayout);
    }

    private void InputText(LinearLayout linLayout, final int featNum, final String featName) {
        LinearLayout linearLayout = new LinearLayout(getContext);
        LinearLayout.LayoutParams layoutParams = new LinearLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT);
        layoutParams.setMargins(7, 5, 7, 5);

        final Button button = new Button(getContext);

        String string = Preferences.loadPrefString(featName, featNum);
        button.setText(Html.fromHtml(featName + ": <font color='" + NumberTxtColor + "'>" + string + "</font>"));

        button.setAllCaps(false);
        button.setLayoutParams(layoutParams);
        button.setBackgroundColor(BTN_COLOR);
        button.setTextColor(TEXT_COLOR_2);
        button.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                AlertDialog.Builder alertName = new AlertDialog.Builder(getContext);

                final EditText editText = new EditText(getContext);
                editText.setOnFocusChangeListener(new View.OnFocusChangeListener() {
                    @Override
                    public void onFocusChange(View v, boolean hasFocus) {
                        InputMethodManager imm = (InputMethodManager) getContext.getSystemService(getContext.INPUT_METHOD_SERVICE);
                        if (hasFocus) {
                            imm.toggleSoftInput(InputMethodManager.SHOW_FORCED, InputMethodManager.HIDE_IMPLICIT_ONLY);
                        } else {
                            imm.toggleSoftInput(InputMethodManager.HIDE_IMPLICIT_ONLY, 0);
                        }
                    }
                });
                editText.requestFocus();

                alertName.setTitle("Input text");
                alertName.setView(editText);
                LinearLayout layoutName = new LinearLayout(getContext);
                layoutName.setOrientation(LinearLayout.VERTICAL);
                layoutName.addView(editText); // displays the user input bar
                alertName.setView(layoutName);

                alertName.setPositiveButton("OK", new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface dialog, int whichButton) {
                        String str = editText.getText().toString();
                        button.setText(Html.fromHtml(featName + ": <font color='" + NumberTxtColor + "'>" + str + "</font>"));
                        Preferences.changeFeatureString(featName, featNum, str);
                        editText.setFocusable(false);
                    }
                });

                alertName.setNegativeButton("Cancel", new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface dialog, int whichButton) {
                        //dialog.cancel(); // closes dialog
                        InputMethodManager imm = (InputMethodManager) getContext.getSystemService(getContext.INPUT_METHOD_SERVICE);
                        imm.toggleSoftInput(InputMethodManager.HIDE_IMPLICIT_ONLY, 0);
                    }
                });


                if (overlayRequired) {
                    AlertDialog dialog = alertName.create(); // display the dialog
                    dialog.getWindow().setType(Build.VERSION.SDK_INT >= 26 ? 2038 : 2002);
                    dialog.show();
                } else {
                    alertName.show();
                }
            }
        });

        linearLayout.addView(button);
        linLayout.addView(linearLayout);
    }

    private void CheckBox(LinearLayout linLayout, final int featNum, final String featName, boolean switchedOn) {
        final CheckBox checkBox = new CheckBox(getContext);
        checkBox.setText(featName);
        checkBox.setTextColor(TEXT_COLOR_2);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP)
            checkBox.setButtonTintList(ColorStateList.valueOf(CheckBoxColor));
        checkBox.setChecked(Preferences.loadPrefBool(featName, featNum, switchedOn));
        checkBox.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                if (checkBox.isChecked()) {
                    Preferences.changeFeatureBool(featName, featNum, isChecked);
                } else {
                    Preferences.changeFeatureBool(featName, featNum, isChecked);
                }
            }
        });
        linLayout.addView(checkBox);
    }

    private void RadioButton(LinearLayout linLayout, final int featNum, String featName, final String list) {
        //Credit: LoraZalora
        final List<String> lists = new LinkedList<>(Arrays.asList(list.split(",")));

        final TextView textView = new TextView(getContext);
        textView.setText(featName + ":");
        textView.setTextColor(TEXT_COLOR_2);

        final RadioGroup radioGroup = new RadioGroup(getContext);
        radioGroup.setPadding(10, 5, 10, 5);
        radioGroup.setOrientation(LinearLayout.VERTICAL);
        radioGroup.addView(textView);

        for (int i = 0; i < lists.size(); i++) {
            final RadioButton Radioo = new RadioButton(getContext);
            final String finalfeatName = featName, radioName = lists.get(i);
            View.OnClickListener first_radio_listener = new View.OnClickListener() {
                public void onClick(View v) {
                    textView.setText(Html.fromHtml(finalfeatName + ": <font color='" + NumberTxtColor + "'>" + radioName));
                    Preferences.changeFeatureInt(finalfeatName, featNum, radioGroup.indexOfChild(Radioo));
                }
            };
            System.out.println(lists.get(i));
            Radioo.setText(lists.get(i));
            Radioo.setTextColor(Color.LTGRAY);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP)
                Radioo.setButtonTintList(ColorStateList.valueOf(RadioColor));
            Radioo.setOnClickListener(first_radio_listener);
            radioGroup.addView(Radioo);
        }

        int index = Preferences.loadPrefInt(featName, featNum);
        if (index > 0) { //Preventing it to get an index less than 1. below 1 = null = crash
            textView.setText(Html.fromHtml(featName + ": <font color='" + NumberTxtColor + "'>" + lists.get(index - 1)));
            ((RadioButton) radioGroup.getChildAt(index)).setChecked(true);
        }
        linLayout.addView(radioGroup);
    }

    private void Collapse(LinearLayout linLayout, final String text, final boolean expanded) {
        LinearLayout.LayoutParams layoutParamsLL = new LinearLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT);
        layoutParamsLL.setMargins(0, 5, 0, 0);

        LinearLayout collapse = new LinearLayout(getContext);
        collapse.setLayoutParams(layoutParamsLL);
        collapse.setVerticalGravity(16);
        collapse.setOrientation(LinearLayout.VERTICAL);

        final LinearLayout collapseSub = new LinearLayout(getContext);
        collapseSub.setVerticalGravity(16);
        collapseSub.setPadding(0, 5, 0, 5);
        collapseSub.setOrientation(LinearLayout.VERTICAL);
        collapseSub.setBackgroundColor(Color.parseColor("#222D38"));
        collapseSub.setVisibility(View.GONE);
        mCollapse = collapseSub;

        final TextView textView = new TextView(getContext);
        textView.setBackgroundColor(CategoryBG);
        textView.setText("▽ " + text + " ▽");
        textView.setGravity(Gravity.CENTER);
        textView.setTextColor(TEXT_COLOR_2);
        textView.setTypeface(null, Typeface.BOLD);
        textView.setPadding(0, 20, 0, 20);

        if (expanded) {
            collapseSub.setVisibility(View.VISIBLE);
            textView.setText("△ " + text + " △");
        }

        textView.setOnClickListener(new View.OnClickListener() {
            boolean isChecked = expanded;

            @Override
            public void onClick(View v) {

                boolean z = !isChecked;
                isChecked = z;
                if (z) {
                    collapseSub.setVisibility(View.VISIBLE);
                    textView.setText("△ " + text + " △");
                    return;
                }
                collapseSub.setVisibility(View.GONE);
                textView.setText("▽ " + text + " ▽");
            }
        });
        collapse.addView(textView);
        collapse.addView(collapseSub);
        linLayout.addView(collapse);
    }

    private void Category(LinearLayout linLayout, String text) {
        TextView textView = new TextView(getContext);
        textView.setBackgroundColor(CategoryBG);
        textView.setText(Html.fromHtml(text));
        textView.setGravity(Gravity.CENTER);
        textView.setTextColor(TEXT_COLOR_2);
        textView.setTypeface(null, Typeface.BOLD);
        textView.setPadding(0, 5, 0, 5);
        linLayout.addView(textView);
    }

    private void TextView(LinearLayout linLayout, String text) {
        TextView textView = new TextView(getContext);
        textView.setText(Html.fromHtml(text));
        textView.setTextColor(TEXT_COLOR_2);
        textView.setPadding(10, 5, 10, 5);
        linLayout.addView(textView);
    }

    private void WebTextView(LinearLayout linLayout, String text) {
        WebView wView = new WebView(getContext);
        wView.loadData(text, "text/html", "utf-8");
        wView.setBackgroundColor(0x00000000); //Transparent
        wView.setPadding(0, 5, 0, 5);
        try {
            wView.getSettings().getClass().getMethod("setAppCacheEnabled", boolean.class).invoke(wView.getSettings(), false);
        } catch (Exception ignored) {
        }
        linLayout.addView(wView);
    }

    private boolean isViewCollapsed() {
        return rootFrame == null || mCollapsed.getVisibility() == View.VISIBLE;
    }

    //For our image a little converter
    private int convertDipToPixels(int i) {
        return (int) ((((float) i) * getContext.getResources().getDisplayMetrics().density) + 0.5f);
    }

    private int dp(int i) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, (float) i, getContext.getResources().getDisplayMetrics());
    }

    public void setVisibility(int view) {
        if (rootFrame != null) {
            rootFrame.setVisibility(view);
        }
        if (hudRootFrame != null) {
            hudRootFrame.setVisibility(view == View.VISIBLE && isHudEnabled ? View.VISIBLE : View.GONE);
        }
    }

    public void onDestroy() {
        if (hudUpdateHandler != null && hudUpdateRunnable != null) {
            hudUpdateHandler.removeCallbacks(hudUpdateRunnable);
        }
        if (hudRootFrame != null && mWindowManager != null) {
            try {
                mWindowManager.removeView(hudRootFrame);
            } catch (Exception ignored) {
            }
        }
        if (rootFrame != null && mWindowManager != null) {
            try {
                mWindowManager.removeView(rootFrame);
            } catch (Exception ignored) {
            }
        }
        logToFile("Menu and HUD Status Panel destroyed.");
    }

    // ========================================================================
    // HUD Status Panel Implementation
    // ========================================================================
    private void initHudStatusPanel(final Context context) {
        hudRootFrame = new FrameLayout(context);
        hudRootFrame.setVisibility(View.GONE); // OFF by default

        int iparams = Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.O ? 2038 : 2002;
        int screenWidth = context.getResources().getDisplayMetrics().widthPixels;
        int panelWidth = dp(240);
        hudParams = new WindowManager.LayoutParams(
                panelWidth,
                WRAP_CONTENT,
                iparams,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                        WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL |
                        WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                PixelFormat.TRANSLUCENT
        );
        hudParams.gravity = 51; // Gravity.TOP | Gravity.LEFT
        hudParams.x = Math.max(0, (screenWidth - panelWidth) / 2); // Top Center
        hudParams.y = dp(16); // Top margin

        hudPanelContainer = new LinearLayout(context);
        hudPanelContainer.setOrientation(LinearLayout.VERTICAL);
        hudPanelContainer.setPadding(dp(10), dp(8), dp(10), dp(8));
        hudPanelContainer.setLayoutParams(new LinearLayout.LayoutParams(panelWidth, WRAP_CONTENT));

        // 100% Solid dark futuristic theme with neon cyan border
        GradientDrawable gd = new GradientDrawable();
        gd.setCornerRadius(dp(8));
        gd.setColor(Color.parseColor("#FF101722")); // 100% Solid Opaque Dark Navy
        gd.setStroke(dp(2), Color.parseColor("#00E5FF")); // Neon Cyan border
        hudPanelContainer.setBackground(gd);
        hudPanelContainer.setAlpha(1.0f); // 100% Opacity

        // 1. Header layout (Title + Status Badge)
        LinearLayout headerLayout = new LinearLayout(context);
        headerLayout.setOrientation(LinearLayout.HORIZONTAL);
        headerLayout.setGravity(Gravity.CENTER_VERTICAL);
        headerLayout.setPadding(0, 0, 0, dp(4));

        hudHeaderTitle = new TextView(context);
        hudHeaderTitle.setText("STATUS RADAR");
        hudHeaderTitle.setTextColor(Color.parseColor("#00E5FF"));
        hudHeaderTitle.setTypeface(Typeface.DEFAULT_BOLD);
        hudHeaderTitle.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        LinearLayout.LayoutParams titleLp = new LinearLayout.LayoutParams(0, WRAP_CONTENT, 1.0f);
        hudHeaderTitle.setLayoutParams(titleLp);

        hudStatusBadge = new TextView(context);
        hudStatusBadge.setText(" STANDBY ");
        hudStatusBadge.setTextColor(Color.WHITE);
        hudStatusBadge.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
        hudStatusBadge.setTypeface(Typeface.DEFAULT_BOLD);
        hudStatusBadge.setPadding(dp(6), dp(2), dp(6), dp(2));

        GradientDrawable badgeGd = new GradientDrawable();
        badgeGd.setCornerRadius(dp(4));
        badgeGd.setColor(Color.parseColor("#37474F"));
        hudStatusBadge.setBackground(badgeGd);

        headerLayout.addView(hudHeaderTitle);
        headerLayout.addView(hudStatusBadge);

        // Divider accent line
        View divider = new View(context);
        divider.setBackgroundColor(Color.parseColor("#4000E5FF"));
        LinearLayout.LayoutParams divLp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(1));
        divLp.setMargins(0, dp(2), 0, dp(4));
        divider.setLayoutParams(divLp);

        // 2. Content layout (Summary row + Scrollable roster list)
        hudContentContainer = new LinearLayout(context);
        hudContentContainer.setOrientation(LinearLayout.VERTICAL);

        // Summary row: TEAM / ENEMIES / ALIVE
        hudSummaryRow = new TextView(context);
        hudSummaryRow.setTextColor(Color.WHITE);
        hudSummaryRow.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        hudSummaryRow.setPadding(0, 0, 0, dp(4));
        hudSummaryRow.setText(Html.fromHtml("<b>TEAM:</b> 0 | <b>ENEMIES:</b> 0 | <b>ALIVE:</b> 0"));

        // Scrollable roster container for player & enemy usernames (Fixed height constraint)
        hudRosterScroll = new ScrollView(context);
        LinearLayout.LayoutParams scrollLp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(115));
        hudRosterScroll.setLayoutParams(scrollLp);

        LinearLayout rosterInner = new LinearLayout(context);
        rosterInner.setOrientation(LinearLayout.VERTICAL);

        // Team section
        hudTeamHeader = new TextView(context);
        hudTeamHeader.setTextColor(Color.parseColor("#00E676"));
        hudTeamHeader.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
        hudTeamHeader.setTypeface(Typeface.DEFAULT_BOLD);
        hudTeamHeader.setText("[ALLIES / TEAM]");
        hudTeamHeader.setPadding(0, dp(2), 0, dp(1));

        hudTeamList = new TextView(context);
        hudTeamList.setTextColor(Color.parseColor("#EEEEEE"));
        hudTeamList.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
        hudTeamList.setPadding(dp(4), 0, 0, dp(3));
        hudTeamList.setText("Waiting for match roster...");

        // Enemy section
        hudEnemyHeader = new TextView(context);
        hudEnemyHeader.setTextColor(Color.parseColor("#FF5252"));
        hudEnemyHeader.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
        hudEnemyHeader.setTypeface(Typeface.DEFAULT_BOLD);
        hudEnemyHeader.setText("[ENEMIES]");
        hudEnemyHeader.setPadding(0, dp(2), 0, dp(1));

        hudEnemyList = new TextView(context);
        hudEnemyList.setTextColor(Color.parseColor("#EEEEEE"));
        hudEnemyList.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
        hudEnemyList.setPadding(dp(4), 0, 0, dp(3));
        hudEnemyList.setText("Waiting for enemy roster...");

        rosterInner.addView(hudTeamHeader);
        rosterInner.addView(hudTeamList);
        rosterInner.addView(hudEnemyHeader);
        rosterInner.addView(hudEnemyList);
        hudRosterScroll.addView(rosterInner);

        hudContentContainer.addView(headerLayout);
        hudContentContainer.addView(divider);
        hudContentContainer.addView(hudSummaryRow);
        hudContentContainer.addView(hudRosterScroll);

        hudPanelContainer.addView(hudContentContainer);
        hudRootFrame.addView(hudPanelContainer, new FrameLayout.LayoutParams(panelWidth, WRAP_CONTENT));

        // Smooth Drag Listener (No accidental compacting on click)
        hudPanelContainer.setOnTouchListener(new View.OnTouchListener() {
            private int initX, initY;
            private float touchX, touchY;
            private boolean isDragging = false;
            private final int touchSlop = dp(3);

            @Override
            public boolean onTouch(View v, MotionEvent event) {
                switch (event.getAction()) {
                    case MotionEvent.ACTION_DOWN:
                        initX = hudParams.x;
                        initY = hudParams.y;
                        touchX = event.getRawX();
                        touchY = event.getRawY();
                        isDragging = false;
                        return true;
                    case MotionEvent.ACTION_MOVE:
                        int dx = (int) (event.getRawX() - touchX);
                        int dy = (int) (event.getRawY() - touchY);
                        if (Math.abs(dx) > touchSlop || Math.abs(dy) > touchSlop) {
                            isDragging = true;
                            hudParams.x = initX + dx;
                            hudParams.y = initY + dy;
                            if (mWindowManager != null && hudRootFrame != null) {
                                try {
                                    mWindowManager.updateViewLayout(hudRootFrame, hudParams);
                                } catch (Exception ignored) {}
                            }
                        }
                        return true;
                    case MotionEvent.ACTION_UP:
                        return true;
                }
                return false;
            }
        });

        // Periodic updater handler (500ms)
        hudUpdateHandler = new Handler(Looper.getMainLooper());
        hudUpdateRunnable = new Runnable() {
            @Override
            public void run() {
                try {
                    updateHudStatus();
                } catch (Throwable t) {
                    Log.e(TAG, "HUD Update Error: " + t.getMessage());
                }
                if (hudUpdateHandler != null) {
                    hudUpdateHandler.postDelayed(this, 500);
                }
            }
        };

        logToFile("HUD Status Panel initialized successfully (Default: OFF, Pos: Top-Center, Opacity: 100%).");
    }

    private void updateHudStatus() {
        if (!isHudEnabled || hudRootFrame == null) {
            return;
        }

        String rawStatus = "";
        try {
            rawStatus = GetHudStatus();
        } catch (UnsatisfiedLinkError e) {
            return;
        }

        int players = 0, bots = 0, alive = 0;
        String status = "LOBBY";
        boolean localAlive = false;

        if (rawStatus != null && !rawStatus.isEmpty()) {
            String[] parts = rawStatus.split("\\|");
            for (String part : parts) {
                String[] kv = part.split(":");
                if (kv.length == 2) {
                    String key = kv[0];
                    String val = kv[1];
                    if ("PLAYERS".equals(key)) {
                        try { players = Integer.parseInt(val); } catch (Exception ignored) {}
                    } else if ("BOTS".equals(key)) {
                        try { bots = Integer.parseInt(val); } catch (Exception ignored) {}
                    } else if ("ALIVE".equals(key)) {
                        try { alive = Integer.parseInt(val); } catch (Exception ignored) {}
                    } else if ("STATUS".equals(key)) {
                        status = val;
                    } else if ("LOCAL_ALIVE".equals(key)) {
                        localAlive = "1".equals(val);
                    }
                }
            }
        }

        boolean inBattle = "ALIVE".equals(status) || "DEAD".equals(status) || status.contains("BATTLE");
        if (inBattle && "ALIVE".equals(status)) {
            localAlive = true;
        } else if ("DEAD".equals(status)) {
            localAlive = false;
        }

        // Update status badge
        if (hudStatusBadge != null) {
            GradientDrawable badgeGd = new GradientDrawable();
            badgeGd.setCornerRadius(dp(4));
            if (!inBattle) {
                hudStatusBadge.setText(" [LOBBY] ");
                badgeGd.setColor(Color.parseColor("#37474F"));
            } else if (localAlive) {
                hudStatusBadge.setText(" [ALIVE] ");
                badgeGd.setColor(Color.parseColor("#1B5E20"));
            } else {
                hudStatusBadge.setText(" [DEAD / SPECTATING] ");
                badgeGd.setColor(Color.parseColor("#B71C1C"));
            }
            hudStatusBadge.setBackground(badgeGd);
        }

        // Fetch team & enemy roster with username
        String rawRoster = "";
        try {
            rawRoster = GetHudRoster();
        } catch (UnsatisfiedLinkError ignored) {}

        int teamCount = 0;
        int enemyCount = 0;
        StringBuilder teamSb = new StringBuilder();
        StringBuilder enemySb = new StringBuilder();

        if (inBattle && rawRoster != null && rawRoster.contains("|")) {
            String[] rParts = rawRoster.split("\\|");
            for (String rp : rParts) {
                if (rp.startsWith("TEAM:")) {
                    String list = rp.substring(5);
                    if (!list.isEmpty()) {
                        String[] members = list.split(",");
                        for (String m : members) {
                            teamCount++;
                            teamSb.append(formatRosterEntry(m, true, !localAlive)).append("<br>");
                        }
                    }
                } else if (rp.startsWith("ENEMY:")) {
                    String list = rp.substring(6);
                    if (!list.isEmpty()) {
                        String[] members = list.split(",");
                        for (String m : members) {
                            enemyCount++;
                            enemySb.append(formatRosterEntry(m, false, false)).append("<br>");
                        }
                    }
                }
            }
        }

        if (inBattle && alive > 0) {
            if (teamCount == 0) teamCount = 1;
            if (enemyCount == 0 && alive > teamCount) {
                enemyCount = alive - teamCount;
            }
        }

        if (!inBattle) {
            teamSb.setLength(0);
            teamSb.append("<font color='#78909C'><i>Lobby / Waiting for battle...</i></font>");
            enemySb.setLength(0);
            enemySb.append("<font color='#78909C'><i>No active match</i></font>");
        } else {
            if (teamCount == 0 && teamSb.length() == 0) {
                String youEntry = localAlive ? "You(Ghost)[P]" : "You(Ghost)[DEAD]";
                teamSb.append(formatRosterEntry(youEntry, true, !localAlive)).append("<br>");
            }
            if (enemyCount == 0 && enemySb.length() == 0) {
                enemySb.append("<font color='#78909C'><i>Scanning hostiles...</i></font>");
            }
        }

        if (hudSummaryRow != null) {
            if (!inBattle) {
                hudSummaryRow.setText(Html.fromHtml("<b>STATUS:</b> <font color='#90A4AE'>STANDBY IN LOBBY</font>"));
            } else {
                String youStatusHtml = localAlive ?
                        "<font color='#00E676'><b>ALIVE</b></font>" :
                        "<font color='#FF5252'><b>ELIMINATED</b></font>";
                hudSummaryRow.setText(Html.fromHtml("<b>YOU:</b> " + youStatusHtml +
                        " | <b>TEAM:</b> <font color='#00E676'><b>" + teamCount + "</b></font>" +
                        " | <b>ENEMIES:</b> <font color='#FF5252'><b>" + enemyCount + "</b></font>" +
                        " | <b>ALIVE:</b> <font color='#00E5FF'><b>" + alive + "</b></font>"));
            }
        }

        if (hudTeamList != null) {
            hudTeamList.setText(Html.fromHtml(teamSb.toString()));
        }
        if (hudEnemyList != null) {
            hudEnemyList.setText(Html.fromHtml(enemySb.toString()));
        }
    }

    private String formatRosterEntry(String entry, boolean isAlly, boolean isLocalDead) {
        // Format: Name(Hero)[P] or Name(Hero)[B] or Name(Hero)[DEAD]
        boolean isDead = entry.contains("[DEAD]") || (isAlly && isLocalDead && (entry.startsWith("You") || entry.contains("(Ghost)")));
        boolean isReal = entry.endsWith("[P]") || entry.contains("[P]");
        String clean = entry.replace("[P]", "").replace("[B]", "").replace("[DEAD]", "");
        String name = clean;
        String hero = "";
        int paren = clean.indexOf('(');
        if (paren != -1 && clean.endsWith(")")) {
            name = clean.substring(0, paren);
            hero = clean.substring(paren + 1, clean.length() - 1);
        }

        if (isDead) {
            String heroStr = hero.isEmpty() ? "" : " <font color='#78909C'>(" + hero + ")</font>";
            return "<font color='#FF5252'>&#9760;</font> <font color='#90A4AE'><s><b>" + name + "</b></s></font>" + heroStr + " <font color='#FF5252'>[DEAD]</font>";
        }

        String dot = isAlly ? "<font color='#00E676'>&#9679;</font>" : "<font color='#FF5252'>&#9679;</font>";
        String tag = isReal ? "<font color='#00E5FF'>[PLAYER]</font>" : "<font color='#FFA726'>[BOT]</font>";
        String heroStr = hero.isEmpty() ? "" : " <font color='#90A4AE'>(" + hero + ")</font>";

        return dot + " <b>" + name + "</b>" + heroStr + " " + tag;
    }

    public static void logToFile(String msg) {
        try {
            File file = new File("/sdcard/Documents/BulletEcho_HUD.log");
            if (!file.exists()) {
                file = new File("/storage/emulated/0/Documents/BulletEcho_HUD.log");
            }
            File parent = file.getParentFile();
            if (parent != null && !parent.exists()) {
                parent.mkdirs();
            }
            SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US);
            String line = "[" + sdf.format(new Date()) + "] [JAVA] [HUD] " + msg + "\n";
            FileOutputStream fos = new FileOutputStream(file, true);
            fos.write(line.getBytes());
            fos.close();
        } catch (Throwable ignored) {
        }
    }
}