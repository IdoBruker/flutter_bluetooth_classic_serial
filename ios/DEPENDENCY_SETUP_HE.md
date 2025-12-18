# הוראות התקנה - שימוש כתלות (Dependency)

## חשוב! קרא זאת תחילה

**מסמך זה מיועד למפתחים שמשתמשים ב-flutter_bluetooth_classic_serial כתלות (dependency) בפרויקט Flutter שלהם.**

אם אתה מפתח את הפלאגין עצמו, ראה את `SETUP_INSTRUCTIONS_HE.md`.

---

## סקירה כללית

כאשר אתה משתמש בפלאגין הזה כתלות, אתה צריך להוסיף הרשאות ל-**האפליקציה שלך**, לא לפלאגין עצמו.

### מה צריך לעשות?

1. ✅ הוסף entitlements file לפרויקט iOS שלך
2. ✅ עדכן את Info.plist של האפליקציה שלך
3. ✅ קשר את ה-entitlements ב-Xcode

---

## שלב 1: הוסף את הפלאגין ל-pubspec.yaml

בפרויקט Flutter שלך:

```yaml
dependencies:
  flutter_bluetooth_classic_serial:
    git: https://github.com/IdoBruker/flutter_bluetooth_classic_serial
```

הרץ:

```bash
flutter pub get
```

---

## שלב 2: צור קובץ Entitlements באפליקציה שלך

### 2.1 צור את הקובץ

נווט לתיקיית הפרויקט שלך וצור קובץ חדש:

```bash
YOUR_APP/ios/Runner/Runner.entitlements
```

### 2.2 תוכן הקובץ

העתק את התוכן הבא לקובץ:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>com.apple.external-accessory.wireless-configuration</key>
	<true/>
</dict>
</plist>
```

---

## שלב 3: עדכן את Info.plist באפליקציה שלך

### 3.1 פתח את הקובץ

```bash
YOUR_APP/ios/Runner/Info.plist
```

### 3.2 הוסף את השורות הבאות

אם הן לא קיימות, הוסף לפני `</dict>` הסופי:

```xml
<key>UISupportedExternalAccessoryProtocols</key>
<array>
    <string>Tigaro.com</string>
</array>

<key>NSBluetoothAlwaysUsageDescription</key>
<string>אפליקציה זו זקוקה ל-Bluetooth כדי לתקשר עם מכשירים חיצוניים (Labdisc, MiniDisc, DataHub, Forceacc)</string>

<key>NSBluetoothPeripheralUsageDescription</key>
<string>אפליקציה זו זקוקה ל-Bluetooth כדי לתקשר עם מכשירים חיצוניים (Labdisc, MiniDisc, DataHub, Forceacc)</string>
```

**הערה:** אתה יכול לשנות את ההסבר בעברית לפי צרכי האפליקציה שלך.

---

## שלב 4: קשר את ה-Entitlements ב-Xcode

### 4.1 פתח את הפרויקט ב-Xcode

```bash
cd YOUR_APP
open ios/Runner.xcworkspace
```

**חשוב:** פתח את `.xcworkspace` ולא `.xcodeproj`

### 4.2 בחר את ה-Target שלך

1. לחץ על הפרויקט הכחול בסרגל הצד (Project Navigator)
2. תחת **TARGETS**, בחר את האפליקציה שלך (בדרך כלל נקראת Runner)

### 4.3 הוסף את ה-Capability

**אופציה א' (מומלצת) - דרך Capabilities:**

1. לחץ על לשונית **Signing & Capabilities**
2. לחץ על **+ Capability** בפינה השמאלית העליונה
3. חפש "**External Accessory Communication**"
4. לחץ עליו כדי להוסיף
5. Xcode יקשר אוטומטית את קובץ ה-entitlements

**אופציה ב' - קישור ידני:**

1. עבור ל-**Build Settings**
2. חפש "**Code Signing Entitlements**"
3. הוסף את הערך: `Runner/Runner.entitlements`
4. עשה זאת עבור Debug, Release, ו-Profile

### 4.4 אמת

1. חזור ל-**Signing & Capabilities**
2. ודא שרואה: **Code Signing Entitlements: Runner/Runner.entitlements**
3. ודא שקובץ `Runner.entitlements` מופיע ב-Project Navigator

---

## שלב 5: ניקוי ובנייה

### 5.1 נקה את הפרויקט

ב-Xcode:

- **Product** → **Clean Build Folder** (או **Cmd+Shift+K**)

### 5.2 בנה והרץ

```bash
flutter clean
flutter pub get
flutter run
```

או ב-Xcode:

- לחץ על כפתור Run (או **Cmd+R**)

---

## שלב 6: בדיקה

### 6.1 זווג מכשיר

1. הגדרות iOS → Bluetooth
2. זווג את מכשיר ה-Labdisc
3. ודא שהוא מופיע כ-"Connected"

### 6.2 הרץ את האפליקציה ובדוק לוגים

אם הכל עובד, תראה בקונסול:

```
[ExternalAccessory] Starting discovery, found 1 connected accessories
[ExternalAccessory] Accessory: Labdisc-12345, serial: XXXXX
[ExternalAccessory] Protocols: ["Tigaro.com"]
[ExternalAccessory] Device Labdisc-12345 is supported
```

במקום:

```
[#ExternalAccessory] Returning connectedAccessories count 0
```

---

## מבנה התיקיות שלך

אחרי ההתקנה, זה צריך להיראות כך:

```
YOUR_APP/
├── ios/
│   ├── Runner/
│   │   ├── Info.plist                 ✅ עודכן
│   │   ├── Runner.entitlements        ✅ נוסף
│   │   └── ...
│   └── Runner.xcworkspace/
├── lib/
├── pubspec.yaml                        ✅ מכיל את הפלאגין
└── ...
```

---

## פתרון בעיות

### עדיין מקבל count 0?

#### 1. ✅ ודא שזיווגת את המכשיר

הגדרות iOS → Bluetooth → ודא ש-Labdisc מחובר (Connected)

#### 2. ✅ בדוק את ה-Entitlements

ב-Xcode → Signing & Capabilities:

- ודא שמופיע "External Accessory Communication" או
- ודא שמופיע "Code Signing Entitlements: Runner/Runner.entitlements"

#### 3. ✅ בדוק את Info.plist

ודא שהמפתחות `UISupportedExternalAccessoryProtocols` ו-`NSBluetoothAlwaysUsageDescription` קיימים ב-**האפליקציה שלך** (לא בפלאגין).

#### 4. ✅ נקה הכל

```bash
# נקה Flutter
flutter clean

# מחק את build
rm -rf ios/Pods
rm -rf ios/.symlinks
rm ios/Podfile.lock

# התקן מחדש
flutter pub get
cd ios && pod install && cd ..

# בנה מחדש
flutter run
```

#### 5. ✅ הסר את האפליקציה מהמכשיר

לפעמים הרשאות לא מתעדכנות. מחק את האפליקציה מהמכשיר והתקן מחדש.

#### 6. ✅ בדוק שאתה על מכשיר אמיתי

External Accessory **לא עובד בסימולטור**. חייב להשתמש במכשיר iOS פיזי.

---

## שגיאות נפוצות

### "Protocol not supported"

המכשיר לא תומך בפרוטוקול הנכון. בדוק את הלוגים:

```
[ExternalAccessory] No supported protocol found for accessory: Labdisc-XXX,
available protocols: ["SomeOtherProtocol.com"]
```

**פתרון:**

1. הוסף את הפרוטוקול האמיתי ל-`Info.plist` שלך:

```xml
<key>UISupportedExternalAccessoryProtocols</key>
<array>
    <string>Tigaro.com</string>
    <string>SomeOtherProtocol.com</string>  <!-- הוסף את הפרוטוקול שמופיע בלוג -->
</array>
```

### "Device not found"

המכשיר לא מזווג או לא מחובר.

**פתרון:**

- הגדרות → Bluetooth → ודא שהמכשיר מחובר
- כבה והדלק את ה-Bluetooth
- נתק וחבר מחדש את המכשיר

### האפליקציה קורסת בהפעלה

בדוק שיש לך את ההרשאות ב-`Info.plist`:

- `NSBluetoothAlwaysUsageDescription`
- `NSBluetoothPeripheralUsageDescription`

iOS דורשת הסברים אלו, אחרת האפליקציה תקרוס.

## תמיכה

אם עדיין יש בעיות:

1. הפעל את האפליקציה ב-Debug mode
2. צרף את כל הלוגים מהקונסול
3. צלם מסך של Signing & Capabilities ב-Xcode
4. ודא שהמכשיר הוא MFi-certified

---

**גרסה:** 1.0.3  
**עודכן לאחרונה:** נובמבר 2025
