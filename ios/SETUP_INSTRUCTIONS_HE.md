# הוראות התקנה - iOS External Accessory

## סקירה כללית

הפרויקט דורש הרשאות מיוחדות (entitlements) כדי לגשת למכשירי External Accessory דרך Bluetooth (Labdisc, MiniDisc, DataHub, Forceacc).

## למה נדרשות הרשאות אלו?

iOS חוסמת גישה למכשירי External Accessory מטעמי אבטחה ופרטיות. ההרשאה `com.apple.external-accessory.wireless-configuration` מעניקה לאפליקציה שלך אישור:

- לזהות אביזרי Bluetooth מסוג MFi (Made for iPhone/iPad) המחוברים
- לתקשר עם אביזרים אלו דרך ה-External Accessory framework
- לגשת לרשימת `connectedAccessories`

**ללא הרשאה זו, `connectedAccessories` תמיד תחזיר מערך רק, גם אם יש מכשירים מזווגים.**

---

## שלב 1: התקנה בפרויקט המרכזי (ios)

### 1.1 פתח את הפרויקט ב-Xcode

```bash
open ios/Runner.xcworkspace
```

**חשוב:** פתח את קובץ ה-`.xcworkspace` ולא את ה-`.xcodeproj`

### 1.2 בחר את ה-Target

1. בסרגל הצד השמאלי (Project Navigator), לחץ על **Runner** (הפרויקט הכחול בראש)
2. בחר את **Runner** target מתחת ל-TARGETS (לא תחת PROJECT)

### 1.3 עבור ללשונית Signing & Capabilities

1. לחץ על הלשונית **Signing & Capabilities** בחלק העליון
2. ודא שיש לך Team נבחר תחת **Signing**

### 1.4 הוסף את קובץ ה-Entitlements

**אופציה א' - דרך ה-Capabilities:**

1. לחץ על כפתור **+ Capability** בפינה השמאלית העליונה
2. חפש "**External Accessory Communication**"
3. לחץ עליו כדי להוסיף
4. Xcode יצור ויקשר אוטומטית את קובץ ה-entitlements

**אופציה ב' - קישור ידני:**

1. עבור ל-**Build Settings** (הלשונית ליד Signing & Capabilities)
2. חפש "**Code Signing Entitlements**" בשורת החיפוש
3. הקלד את הערך: `Runner/Runner.entitlements`
4. עשה זאת עבור כל ה-configurations: Debug, Release, Profile

### 1.5 אמת את הקובץ

1. חזור ל-**Signing & Capabilities**
2. וודא שרואה: **Code Signing Entitlements: Runner/Runner.entitlements**
3. וודא שקובץ `Runner.entitlements` מופיע ב-Project Navigator תחת תיקיית Runner

---

## שלב 2: התקנה באפליקציית הדוגמה (example)

חזור על אותם שלבים עבור אפליקציית הדוגמה:

### 2.1 פתח את פרויקט הדוגמה

```bash
open example/ios/Runner.xcworkspace
```

### 2.2 חזור על שלבים 1.2-1.5

בצע את כל השלבים מלמעלה עבור הפרויקט של example.

---

## שלב 3: ניקוי ובנייה מחדש

### 3.1 נקה את הפרויקט

ב-Xcode:

1. בחר **Product** מהתפריט העליון
2. לחץ על **Clean Build Folder** (או לחץ: **Cmd+Shift+K**)

### 3.2 בנה והרץ

1. בחר מכשיר iOS פיזי (לא סימולטור - External Accessory לא עובד בסימולטור)
2. לחץ על כפתור ה-Run (או **Cmd+R**)

---

## שלב 4: בדיקה

### 4.1 זווג מכשיר Labdisc

1. עבור להגדרות iOS → Bluetooth
2. זווג את מכשיר ה-Labdisc
3. וודא שהוא מופיע כ-"Connected"

### 4.2 הרץ את האפליקציה

1. פתח את האפליקציה
2. בדוק את הלוגים ב-Xcode Console
3. חפש שורות המתחילות ב-`[ExternalAccessory]`

### 4.3 לוגים מצופים

אם הכל עובד, אמור לראות:

```
[ExternalAccessory] Returning connectedAccessories count: 1
[ExternalAccessory] Accessory name: Labdisc-12345
[ExternalAccessory] Serial: XXXXX
[ExternalAccessory] Protocols: ["Tigaro.com"]
```

---

## פתרון בעיות

### עדיין מקבל count 0?

#### ✅ בדוק זיווג המכשיר

- הגדרות iOS → Bluetooth
- ודא שה-Labdisc מחובר (לא רק מזווג)

#### ✅ בדוק את ה-Entitlements

- Xcode → Runner target → Signing & Capabilities
- ודא שמופיע: "Code Signing Entitlements: Runner/Runner.entitlements"

#### ✅ בדוק את Info.plist

וודא שקיימים המפתחות הבאים ב-`ios/Runner/Info.plist`:

```xml
<key>UISupportedExternalAccessoryProtocols</key>
<array>
    <string>Tigaro.com</string>
</array>

<key>NSBluetoothAlwaysUsageDescription</key>
<string>This app needs Bluetooth to communicate with external devices</string>
```

#### ✅ נקה Derived Data

לפעמים Xcode שומר קבצים ישנים:

1. Xcode → **Preferences** → **Locations**
2. לחץ על החץ ליד **Derived Data**
3. מחק את תיקיית הפרויקט שלך
4. בנה מחדש

#### ✅ הסר והתקן מחדש

1. מחק את האפליקציה מהמכשיר
2. בנה והתקן מחדש

### שגיאת "Protocol not supported"?

פרוטוקול המכשיר חייב להכיל "igaro" (case-insensitive).

אם המכשיר משתמש בפרוטוקול אחר:

1. בדוק את הלוגים לפרוטוקולים הזמינים
2. עדכן את `PROTOCOL_SUBSTRING` ב-`SwiftFlutterBluetoothClassicPlugin.swift`
3. הוסף את הפרוטוקול ל-`Info.plist` → `UISupportedExternalAccessoryProtocols`

---

## קבצים שנוצרו/שונו

### קבצים חדשים:

- ✅ `ios/Runner/Runner.entitlements`
- ✅ `example/ios/Runner/Runner.entitlements`

### קבצים ששונו:

- ✅ `ios/Classes/SwiftFlutterBluetoothClassicPlugin.swift`
  - זיהוי פרוטוקול גמיש (כמו בקוד המקורי)
  - הוספת לוגים מפורטים

### תיעוד:

- 📄 `ios/ENTITLEMENTS_SETUP.md` (אנגלית)
- 📄 `ios/SETUP_INSTRUCTIONS_HE.md` (קובץ זה)
- 📄 `ios/README_CHANGES.md`

---

## קישורים שימושיים

- [Apple: External Accessory Framework](https://developer.apple.com/documentation/externalaccessory)
- [Apple: Communicating with External Accessories](https://developer.apple.com/documentation/externalaccessory/communicating_with_external_accessories)

---

## תמיכה נוספת

אם עדיין יש בעיות:

1. שלח את הלוגים המלאים מ-Xcode Console
2. צלם מסך של Signing & Capabilities
3. בדוק שהמכשיר הוא MFi-certified
