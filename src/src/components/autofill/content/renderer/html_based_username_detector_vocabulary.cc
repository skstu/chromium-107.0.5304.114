// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/content/renderer/html_based_username_detector_vocabulary.h"

#include <iterator>

namespace autofill {

const char* const kNegativeLatin[] = {
    "pin",    "parola",   "wagwoord",   "wachtwoord",
    "fake",   "parole",   "givenname",  "achinsinsi",
    "token",  "parool",   "firstname",  "facalfaire",
    "fname",  "lozinka",  "pasahitza",  "focalfaire",
    "lname",  "passord",  "pasiwedhi",  "iphasiwedi",
    "geslo",  "huahuna",  "passwuert",  "katalaluan",
    "heslo",  "fullname", "phasewete",  "adgangskode",
    "parol",  "optional", "wachtwurd",  "contrasenya",
    "sandi",  "lastname", "cyfrinair",  "contrasinal",
    "senha",  "kupuhipa", "katasandi",  "kalmarsirri",
    "hidden", "password", "loluszais",  "tenimiafina",
    "second", "passwort", "middlename", "paroladordine",
    "codice", "pasvorto", "familyname", "inomboloyokuvula",
    "modpas", "salasana", "motdepasse", "numeraeleiloaesesi"};
const size_t kNegativeLatinSize = std::size(kNegativeLatin);

const char* const kNegativeNonLatin[] = {"fjalëkalim",
                                         "የይለፍቃል",
                                         "كلمهالسر",
                                         "գաղտնաբառ",
                                         "пароль",
                                         "পাসওয়ার্ড",
                                         "парола",
                                         "密码",
                                         "密碼",
                                         "დაგავიწყდათ",
                                         "κωδικόςπρόσβασης",
                                         "પાસવર્ડ",
                                         "סיסמה",
                                         "पासवर्ड",
                                         "jelszó",
                                         "lykilorð",
                                         "paswọọdụ",
                                         "パスワード",
                                         "ಪಾಸ್ವರ್ಡ್",
                                         "пароль",
                                         "ការពាក្យសម្ងាត់",
                                         "암호",
                                         "şîfre",
                                         "купуясөз",
                                         "ລະຫັດຜ່ານ",
                                         "slaptažodis",
                                         "лозинка",
                                         "पासवर्ड",
                                         "нууцүг",
                                         "စကားဝှက်ကို",
                                         "पासवर्ड",
                                         "رمز",
                                         "کلمهعبور",
                                         "hasło",
                                         "пароль",
                                         "лозинка",
                                         "پاسورڊ",
                                         "මුරපදය",
                                         "contraseña",
                                         "lösenord",
                                         "гузарвожа",
                                         "கடவுச்சொல்",
                                         "పాస్వర్డ్",
                                         "รหัสผ่าน",
                                         "пароль",
                                         "پاسورڈ",
                                         "mậtkhẩu",
                                         "פּאַראָל",
                                         "ọrọigbaniwọle"};
const size_t kNegativeNonLatinSize = std::size(kNegativeNonLatin);

const char* const kUsernameLatin[] = {
    "gatti",      "uzantonomo",   "solonanarana",    "nombredeusuario",
    "olumulo",    "nomenusoris",  "enwdefnyddiwr",   "nomdutilisateur",
    "lolowera",   "notandanafn",  "nomedeusuario",   "vartotojovardas",
    "username",   "ahanjirimara", "gebruikersnaam",  "numedeutilizator",
    "brugernavn", "benotzernumm", "jinalamtumiaji",  "erabiltzaileizena",
    "brukernavn", "benutzername", "sunanmaiamfani",  "foydalanuvchinomi",
    "mosebedisi", "kasutajanimi", "ainmcleachdaidh", "igamalomsebenzisi",
    "nomdusuari", "lomsebenzisi", "jenengpanganggo", "ingoakaiwhakamahi",
    "nomeutente", "namapengguna"};
const size_t kUsernameLatinSize = std::size(kUsernameLatin);

const char* const kUsernameNonLatin[] = {"用户名",
                                         "کاتيجونالو",
                                         "用戶名",
                                         "የተጠቃሚስም",
                                         "логин",
                                         "اسمالمستخدم",
                                         "נאמען",
                                         "کاصارفکانام",
                                         "ユーザ名",
                                         "όνομα χρήστη",
                                         "brûkersnamme",
                                         "корисничкоиме",
                                         "nonitilizatè",
                                         "корисничкоиме",
                                         "ngaranpamaké",
                                         "ຊື່ຜູ້ໃຊ້",
                                         "användarnamn",
                                         "యూజర్పేరు",
                                         "korisničkoime",
                                         "пайдаланушыаты",
                                         "שםמשתמש",
                                         "ім'якористувача",
                                         "کارننوم",
                                         "хэрэглэгчийннэр",
                                         "nomedeusuário",
                                         "имяпользователя",
                                         "têntruynhập",
                                         "பயனர்பெயர்",
                                         "ainmúsáideora",
                                         "ชื่อผู้ใช้",
                                         "사용자이름",
                                         "імякарыстальніка",
                                         "lietotājvārds",
                                         "потребителскоиме",
                                         "uporabniškoime",
                                         "колдонуучунунаты",
                                         "kullanıcıadı",
                                         "පරිශීලකනාමය",
                                         "istifadəçiadı",
                                         "օգտագործողիանունը",
                                         "navêbikarhêner",
                                         "ಬಳಕೆದಾರಹೆಸರು",
                                         "emriipërdoruesit",
                                         "वापरकर्तानाव",
                                         "käyttäjätunnus",
                                         "વપરાશકર્તાનામ",
                                         "felhasználónév",
                                         "उपयोगकर्तानाम",
                                         "nazwaużytkownika",
                                         "ഉപയോക്തൃനാമം",
                                         "სახელი",
                                         "အသုံးပြုသူအမည်",
                                         "نامکاربری",
                                         "प्रयोगकर्तानाम",
                                         "uživatelskéjméno",
                                         "ব্যবহারকারীরনাম",
                                         "užívateľskémeno",
                                         "ឈ្មោះអ្នកប្រើប្រាស់"};
const size_t kUsernameNonLatinSize = std::size(kUsernameNonLatin);

const char* const kUserLatin[] = {
    "user",   "wosuta",   "gebruiker",  "utilizator",
    "usor",   "notandi",  "gumagamit",  "vartotojas",
    "fammi",  "olumulo",  "maiamfani",  "cleachdaidh",
    "utent",  "pemakai",  "mpampiasa",  "umsebenzisi",
    "bruger", "usuario",  "panganggo",  "utilisateur",
    "bruker", "benotzer", "uporabnik",  "doutilizador",
    "numake", "benutzer", "covneegsiv", "erabiltzaile",
    "usuari", "kasutaja", "defnyddiwr", "kaiwhakamahi",
    "utente", "korisnik", "mosebedisi", "foydalanuvchi",
    "uzanto", "pengguna", "mushandisi"};
const size_t kUserLatinSize = std::size(kUserLatin);

const char* const kUserNonLatin[] = {"用户",
                                     "użytkownik",
                                     "tagatafaʻaaogā",
                                     "دکارونکيعکس",
                                     "用戶",
                                     "užívateľ",
                                     "корисник",
                                     "карыстальнік",
                                     "brûker",
                                     "kullanıcı",
                                     "истифода",
                                     "អ្នកប្រើ",
                                     "ọrụ",
                                     "ተጠቃሚ",
                                     "באַניצער",
                                     "хэрэглэгчийн",
                                     "يوزر",
                                     "istifadəçi",
                                     "ຜູ້ໃຊ້",
                                     "пользователь",
                                     "صارف",
                                     "meahoʻohana",
                                     "потребител",
                                     "वापरकर्ता",
                                     "uživatel",
                                     "ユーザー",
                                     "מִשׁתַמֵשׁ",
                                     "ผู้ใช้งาน",
                                     "사용자",
                                     "bikaranîvan",
                                     "колдонуучу",
                                     "વપરાશકર્તા",
                                     "përdorues",
                                     "ngườidùng",
                                     "корисникот",
                                     "उपयोगकर्ता",
                                     "itilizatè",
                                     "χρήστης",
                                     "користувач",
                                     "օգտվողիանձնագիրը",
                                     "használó",
                                     "faoiúsáideoir",
                                     "შესახებ",
                                     "ব্যবহারকারী",
                                     "lietotājs",
                                     "பயனர்",
                                     "ಬಳಕೆದಾರ",
                                     "ഉപയോക്താവ്",
                                     "کاربر",
                                     "యూజర్",
                                     "පරිශීලක",
                                     "प्रयोगकर्ता",
                                     "användare",
                                     "المستعمل",
                                     "пайдаланушы",
                                     "အသုံးပြုသူကို",
                                     "käyttäjä"};
const size_t kUserNonLatinSize = std::size(kUserNonLatin);

const char* const kTechnicalWords[] = {
    "uid",         "newtel",     "uaccount",   "regaccount",  "ureg",
    "loginid",     "laddress",   "accountreg", "regid",       "regname",
    "loginname",   "membername", "uname",      "ucreate",     "loginmail",
    "accountname", "umail",      "loginreg",   "accountid",   "loginaccount",
    "ulogin",      "regemail",   "newmobile",  "accountlogin"};
const size_t kTechnicalWordsSize = std::size(kTechnicalWords);

const char* const kWeakWords[] = {"id", "login", "mail"};
const size_t kWeakWordsSize = std::size(kWeakWords);

}  // namespace autofill
