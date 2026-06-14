/**
 * BLE keys the firmware can emit (ble_handler.cpp → Generic.kl / vendor .kl).
 * Used for autocomplete in the config editor; Protocol field accepts any string
 * the firmware normalizeBleKeyName() understands (KEYCODE_* aliases included).
 */
(function () {
  const buttonKeys = [];
  for (let i = 1; i <= 16; i++) {
    buttonKeys.push({
      id: `BUTTON_${i}`,
      label: `BUTTON_${i} (KEYCODE_BUTTON_${i})`,
    });
  }

  const videoAppKeys = [];
  for (let i = 1; i <= 16; i++) {
    videoAppKeys.push({
      id: `VIDEO_APP_${i}`,
      label: `VIDEO_APP_${i} → BUTTON_${i}`,
    });
  }

  const digitKeys = '0123456789'.split('').map((d) => ({
    id: d,
    label: `Digit ${d} (keyboard HID)`,
  }));

  const letterKeys = [];
  for (let c = 65; c <= 90; c++) {
    const ch = String.fromCharCode(c);
    letterKeys.push({ id: ch, label: `Letter ${ch} (keyboard HID)` });
  }

  /** Common KEYCODE_* spellings accepted by firmware normalizeBleKeyName(). */
  const keycodeAliases = [
    { id: 'KEYCODE_DPAD_UP', label: 'KEYCODE_DPAD_UP' },
    { id: 'KEYCODE_DPAD_DOWN', label: 'KEYCODE_DPAD_DOWN' },
    { id: 'KEYCODE_DPAD_LEFT', label: 'KEYCODE_DPAD_LEFT' },
    { id: 'KEYCODE_DPAD_RIGHT', label: 'KEYCODE_DPAD_RIGHT' },
    { id: 'KEYCODE_DPAD_CENTER', label: 'KEYCODE_DPAD_CENTER / OK' },
    { id: 'KEYCODE_ENTER', label: 'KEYCODE_ENTER' },
    { id: 'KEYCODE_BACK', label: 'KEYCODE_BACK' },
    { id: 'KEYCODE_HOME', label: 'KEYCODE_HOME' },
    { id: 'KEYCODE_MENU', label: 'KEYCODE_MENU' },
    { id: 'KEYCODE_SEARCH', label: 'KEYCODE_SEARCH' },
    { id: 'KEYCODE_ASSIST', label: 'KEYCODE_ASSIST' },
    { id: 'KEYCODE_APP_SWITCH', label: 'KEYCODE_APP_SWITCH' },
    { id: 'KEYCODE_VOLUME_UP', label: 'KEYCODE_VOLUME_UP' },
    { id: 'KEYCODE_VOLUME_DOWN', label: 'KEYCODE_VOLUME_DOWN' },
    { id: 'KEYCODE_VOLUME_MUTE', label: 'KEYCODE_VOLUME_MUTE' },
    { id: 'KEYCODE_MEDIA_PLAY_PAUSE', label: 'KEYCODE_MEDIA_PLAY_PAUSE' },
    { id: 'KEYCODE_MEDIA_PLAY', label: 'KEYCODE_MEDIA_PLAY' },
    { id: 'KEYCODE_MEDIA_PAUSE', label: 'KEYCODE_MEDIA_PAUSE' },
    { id: 'KEYCODE_MEDIA_STOP', label: 'KEYCODE_MEDIA_STOP' },
    { id: 'KEYCODE_MEDIA_NEXT', label: 'KEYCODE_MEDIA_NEXT' },
    { id: 'KEYCODE_MEDIA_PREVIOUS', label: 'KEYCODE_MEDIA_PREVIOUS' },
    { id: 'KEYCODE_MEDIA_FAST_FORWARD', label: 'KEYCODE_MEDIA_FAST_FORWARD' },
    { id: 'KEYCODE_MEDIA_REWIND', label: 'KEYCODE_MEDIA_REWIND' },
    { id: 'KEYCODE_MEDIA_RECORD', label: 'KEYCODE_MEDIA_RECORD' },
    { id: 'KEYCODE_CHANNEL_UP', label: 'KEYCODE_CHANNEL_UP' },
    { id: 'KEYCODE_CHANNEL_DOWN', label: 'KEYCODE_CHANNEL_DOWN' },
    { id: 'KEYCODE_GUIDE', label: 'KEYCODE_GUIDE' },
    { id: 'KEYCODE_TV', label: 'KEYCODE_TV' },
    { id: 'KEYCODE_POWER', label: 'KEYCODE_POWER' },
    { id: 'KEYCODE_SLEEP', label: 'KEYCODE_SLEEP' },
    { id: 'KEYCODE_PROG_RED', label: 'KEYCODE_PROG_RED' },
    { id: 'KEYCODE_PROG_GREEN', label: 'KEYCODE_PROG_GREEN' },
    { id: 'KEYCODE_PROG_YELLOW', label: 'KEYCODE_PROG_YELLOW' },
    { id: 'KEYCODE_PROG_BLUE', label: 'KEYCODE_PROG_BLUE' },
    { id: 'KEYCODE_NOTIFICATION', label: 'KEYCODE_NOTIFICATION' },
    { id: 'KEYCODE_PROFILE_SWITCH', label: 'KEYCODE_PROFILE_SWITCH' },
    { id: 'KEYCODE_CAPTIONS', label: 'KEYCODE_CAPTIONS' },
  ];

  window.OMOTE_BLE_KEY_CATALOG = [
    {
      group: 'Navigation (DPAD)',
      keys: [
        { id: 'UP', label: 'DPAD_UP' },
        { id: 'DOWN', label: 'DPAD_DOWN' },
        { id: 'LEFT', label: 'DPAD_LEFT' },
        { id: 'RIGHT', label: 'DPAD_RIGHT' },
        { id: 'DPAD_CENTER', label: 'DPAD_CENTER / OK / SELECT' },
        { id: 'ENTER', label: 'ENTER → DPAD_CENTER' },
        { id: 'BACK', label: 'BACK' },
        { id: 'ESC', label: 'ESC → BACK' },
        { id: 'HOME', label: 'HOME' },
        { id: 'MENU', label: 'MENU' },
        { id: 'SEARCH', label: 'SEARCH' },
        { id: 'APP_SWITCH', label: 'APP_SWITCH (recents)' },
        { id: 'ALL_APPS', label: 'ALL_APPS → APP_SWITCH' },
      ],
    },
    {
      group: 'Volume & media',
      keys: [
        { id: 'VOLUME_UP', label: 'VOLUME_UP' },
        { id: 'VOLUME_DOWN', label: 'VOLUME_DOWN' },
        { id: 'MUTE', label: 'VOLUME_MUTE' },
        { id: 'PLAY_PAUSE', label: 'MEDIA_PLAY_PAUSE' },
        { id: 'PLAY', label: 'MEDIA_PLAY' },
        { id: 'PAUSE', label: 'MEDIA_PAUSE' },
        { id: 'STOP', label: 'MEDIA_STOP' },
        { id: 'NEXT', label: 'MEDIA_NEXT' },
        { id: 'PREVIOUS', label: 'MEDIA_PREVIOUS' },
        { id: 'FORWARD', label: 'MEDIA_FAST_FORWARD' },
        { id: 'REWIND', label: 'MEDIA_REWIND' },
        { id: 'RECORD', label: 'MEDIA_RECORD' },
        { id: 'MEDIA_RECORD', label: 'MEDIA_RECORD (alias)' },
        { id: 'MEDIA_AUDIO_TRACK', label: 'MEDIA_AUDIO_TRACK (Generic.kl)' },
      ],
    },
    {
      group: 'TV / live',
      keys: [
        { id: 'CHANNEL_UP', label: 'CHANNEL_UP' },
        { id: 'CHANNEL_DOWN', label: 'CHANNEL_DOWN' },
        { id: 'GUIDE', label: 'GUIDE (EPG)' },
        { id: 'INFO', label: 'INFO (profile-dependent)' },
        { id: 'CAPTIONS', label: 'CAPTIONS' },
        { id: 'TV', label: 'TV' },
        { id: 'LIVE_TV', label: 'LIVE / LIVE_TV' },
        { id: 'NOTIFICATION', label: 'NOTIFICATION' },
        { id: 'PROFILE_SWITCH', label: 'PROFILE_SWITCH' },
        { id: 'POWER', label: 'POWER (sleep/wake)' },
        { id: 'TV_POWER', label: 'TV_POWER' },
        { id: 'SLEEP', label: 'SLEEP' },
        { id: 'PROG_RED', label: 'PROG_RED' },
        { id: 'PROG_GREEN', label: 'PROG_GREEN' },
        { id: 'PROG_YELLOW', label: 'PROG_YELLOW' },
        { id: 'PROG_BLUE', label: 'PROG_BLUE' },
        { id: 'TV_TELETEXT', label: 'TV_TELETEXT (profile-dependent)' },
        { id: 'WWW_HOME', label: 'WWW_HOME / browser' },
        { id: 'EXPLORER', label: 'EXPLORER → WWW_HOME' },
        { id: 'BROWSER', label: 'BROWSER → WWW_HOME' },
        { id: 'SETTINGS', label: 'SETTINGS (may need vendor .kl)' },
        { id: 'TV_INPUT', label: 'TV_INPUT (may need vendor .kl)' },
        { id: 'DVR', label: 'DVR (may need vendor .kl)' },
      ],
    },
    {
      group: 'Assistant & editing',
      keys: [
        { id: 'ASSIST', label: 'ASSIST / voice' },
        { id: 'VOICE_ASSIST', label: 'VOICE_ASSIST → ASSIST' },
        { id: 'TAB', label: 'TAB' },
        { id: 'PAGE_UP', label: 'PAGE_UP' },
        { id: 'PAGE_DOWN', label: 'PAGE_DOWN' },
        { id: 'BACKSPACE', label: 'BACKSPACE / DEL' },
        { id: 'DELETE', label: 'FORWARD_DEL' },
        { id: 'ENTER_TEXT', label: 'ENTER_TEXT (newline)' },
        { id: 'END', label: 'END' },
        { id: 'INSERT', label: 'INSERT' },
      ],
    },
    {
      group: 'Streaming app shortcuts',
      keys: [
        { id: 'NETFLIX', label: 'Netflix → BUTTON_4 (GTV)' },
        { id: 'YOUTUBE', label: 'YouTube → BUTTON_3 (GTV)' },
        { id: 'PRIME_VIDEO', label: 'Prime → BUTTON_6' },
        { id: 'DISNEY_PLUS', label: 'Disney+ → BUTTON_7' },
        { id: 'SPOTIFY', label: 'Spotify → BUTTON_8' },
        { id: 'HBO_MAX', label: 'HBO Max → BUTTON_9' },
        { id: 'MAX', label: 'Max → BUTTON_9' },
        { id: 'APPLE_TV', label: 'Apple TV → BUTTON_10' },
        { id: 'JELLYFIN', label: 'Jellyfin → BUTTON_11' },
      ],
    },
    { group: 'Gamepad BUTTON_1–16', keys: buttonKeys },
    { group: 'VIDEO_APP_N shortcuts', keys: videoAppKeys },
    { group: 'Keyboard digits', keys: digitKeys },
    { group: 'Keyboard letters', keys: letterKeys },
    { group: 'KEYCODE_* aliases', keys: keycodeAliases },
  ];
})();
