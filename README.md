# AZ Monitoring

**Azimxodjayev Music** — vokal zanjiri uchun tahlil va tashxis plagini.

AZ Monitoring vokal insert zanjirining oxiriga qo'yiladi. U ovozni xalqaro o'lchov standartlari va janr etaloni bilan solishtiradi, so'ng nima ideal ekanini va nima tuzatilishi kerakligini aniq raqamlar bilan aytadi. Zanjirga probe'lar qo'yilsa, har bir plaginni alohida baholaydi: qaysi biri me'yorida ishlayapti, qaysi biri ortiqcha siqyapti, qaysi ikkitasi bir-birining ishini bekor qilyapti.

| Format | DAW |
|---|---|
| **VST3** | Cubase / Nuendo, Ableton Live, FL Studio, Studio One, Reaper, Bitwig |
| **AU** | Logic Pro, GarageBand, Ableton Live (Mac) |
| **AAX Native** | Pro Tools (Studio, Ultimate, HD Native, HDX) — AAX SDK va imzo kerak, pastga qarang |

Windows 10/11 (x64) va macOS 11+ (Apple Silicon + Intel, universal binary) qo'llab-quvvatlanadi.

---

## Nima o'lchaydi

**Standartlar bo'yicha (aniq, tekshirilgan):**
- Loudness — ITU-R BS.1770-4 / EBU R128: Momentary, Short-term, Integrated (gating bilan)
- Loudness Range (LRA) — EBU Tech 3342
- True Peak — BS.1770 Annex 2 usulida oversampling (48 kHz da 4x, 96 kHz da 2x)
- Crest factor (dinamika), clipping, shovqin foni
- 44.1 / 48 / 88.2 / 96 / 176.4 / 192 / 384 kHz va 64-bit (double precision) ishlov

Loudness o'lchovi EBU Tech 3341 test signallarida ±0.03 LU aniqlikda, LRA esa Tech 3342 testlarida aniq qiymatda o'tdi. Testlar `tests/DspTests.cpp` faylida.

**Janr etaloni bo'yicha (vokal treki):**
- Spektral balans 1/6 oktava polosalarda etalon koridori bilan solishtiriladi: past chastota (<100 Hz), Mud/Box (200–500 Hz), Nazal (0.7–1.5 kHz), Presence (2–5 kHz), Air (10–16 kHz)
- Sibilyant (S/Sh, 5–9 kHz) — vaqt belgilari bilan
- Har bir muammo uchun aniq tavsiya beriladi: qaysi chastota, qancha dB, qanday Q

**Master rejimida:** loudness tanlangan standartga solishtiriladi (Spotify/YouTube −14, Apple Music −16, EBU R128 −23, ATSC −24). True Peak chegarasi va PLR ham tekshiriladi.

## AZ Rack: plaginlaringiz AZ Monitoring ichida (v0.2)

**Rack · Zanjir** sahifasida **"+ Plagin qo'shish"** tugmasini bosing. Kompyuteringizga o'rnatilgan har qanday VST3 yoki AU plaginni AZ Monitoring ichiga yuklash mumkin, xuddi rack plaginlardagidek. Plaginlar rack ichida bo'lgani uchun AZ Monitoring quyidagilarni **aniq** biladi:

- **Plagin nomi va formati**: VST3, AU yoki UAD-2.
- **Signalga nima qilgani**, har bir plagin uchun alohida.
- **Haqiqiy gain reduction va samarali ratio.** Plaginning kirish va chiqish signali bir vaqtda solishtiriladi, shuning uchun bu qiymatlar o'lchanadi, taxmin qilinmaydi.
- **Parametrlari**: Threshold, Ratio, Attack, Release, Gain va boshqalar to'g'ridan-to'g'ri o'qiladi.

Imkoniyatlar:

- **Tavsiyani qo'llash.** Masalan, haddan ortiq siqayotgan kompressor uchun AZ aniq o'zgarishlarni taklif qiladi: "Ratio 12:1 → 4:1, Attack 0.5 → 15 ms". Avval o'zgarishlar ro'yxati ko'rsatiladi, siz tasdiqlaganingizdan keyingina qo'llanadi. **Qaytarish** tugmasi oldingi sozlamani tiklaydi.
- **Delta tinglash.** Faqat tanlangan plagin olib tashlagan yoki qo'shgan ovozni eshitasiz (kechikish kompensatsiyasi bilan).
- **Bypass, sudrab tartiblash, plagin oynasi, bundan keyin qo'shish, olib tashlash.**
- **PDC.** Rack ichidagi plaginlarning kechikishi jamlanib, DAW'ga avtomatik xabar qilinadi.
- **Preset.** Butun zanjirni `.azrack` fayl sifatida saqlash va boshqa loyihaga yuklash mumkin. Loyiha qayta ochilganda rack plaginlari va ularning sozlamalari avtomatik tiklanadi. Agar biror plagin boshqa kompyuterda o'rnatilmagan bo'lsa, uning sozlamalari baribir yo'qolmaydi.
- **Plagin qo'shish oynasida** qidiruv va toifalar bor (EQ, Dinamika, De-esser, Saturatsiya, Reverb/Delay, UAD-2). Bundan tashqari **AZ tavsiyasi** chiqadi: tashxisga ko'ra zanjirga nima yetishmayotgani va sizda o'rnatilganlardan qaysi biri mos kelishi.

**Birinchi ishga tushirish.** Plagin qo'shish oynasida **"Qayta skanerlash"** tugmasini bosing. AZ kompyuteringizdagi plaginlarni bir marta ro'yxatga oladi. Agar biror plagin skanerlash paytida DAW'ni yopib yuborsa, keyingi safar u avtomatik o'tkazib yuboriladi.

**Cheklovlar:**
- **Faqat AAX formatidagi plaginlar** (Avid Dyn3, EQ3, Pro Compressor va boshqalar) hech qanday boshqa plagin ichiga yuklanmaydi. Bu Avid'ning cheklovi. Bunday plaginlarni Pro Tools insertida qoldiring va oldiga Probe qo'ying: ular Rack sahifasida **DAW** belgisi bilan ko'rinadi.
- **UAD-2 plaginlari** VST3/AU versiyasi orqali rack'da ishlaydi va UAD DSP kartangizdan foydalanadi.
- **Tavsiyani avtomatik qo'llash** parametrlari matn ko'rinishida berilgan plaginlarda ishlaydi, masalan "4:1", "15 ms", "-18 dB". Knoblari 1–7 kabi raqamli shkalaga ega plaginlarda (masalan, ba'zi 1176 emulyatsiyalari) AZ buni ochiq aytadi va tavsiyani qo'lda qo'llashni so'raydi.

## Zanjir rejimi: DAW insertlaridagi plaginlar (Probe)

Bitta plagin ikki rejimda ishlaydi:
- **Monitor** — eng oxirgi slotda turadi va hamma natijani ko'rsatadi.
- **Probe** — har bir plagindan keyin turadigan o'lchov nuqtasi. Signalni o'zgartirmaydi (kirish = chiqish) va kechikish qo'shmaydi.

Har bir probe o'zidan oldingi probe bilan solishtiriladi. Natijada har bir plagin uchun quyidagilar aniqlanadi:
- **Vazifasi**, signalning o'zidan: Kompressor / EQ / De-esser / Gain / Ta'sirsiz
- **Nima qildi**: daraja o'zgarishi, crest (siqish chuqurligi), chastota o'zgarishi, sibilyant o'zgarishi
- **Me'yor bahosi** (0–100) va holati: IDEAL / DIQQAT / MUAMMO / ORTIQCHA

Topiladigan muammolar:
- haddan ortiq siqish
- EQ konflikti (bir plagin kesgan joyni boshqasi ko'targan)
- keskin EQ yoki achchiq presence boost
- sibilyantni kuchaytirish
- yetarli ishlamayotgan de-esser
- shovqin qo'shilishi
- analog emulyatsiya plaginlari uchun noto'g'ri kirish darajasi
- ichki overload
- ta'siri yo'q (ortiqcha) plagin

### O'rnatish tartibi (misol: Pro Tools)

| Slot | Plagin | AZ Monitoring rejimi |
|---|---|---|
| A | AZ Monitoring | Probe → **IN** |
| B | EQ | — |
| C | AZ Monitoring | Probe → **B dan keyin**, nomi: "EQ3 7-Band" |
| D | UAD LA-2A | — |
| E | AZ Monitoring | Probe → **D dan keyin**, nomi: "UAD LA-2A" |
| … | … | … |
| J | AZ Monitoring | **Monitor** |

So'ng trekni boshidan ijro eting va **Qayta tahlil** tugmasini bosing. Natija Monitor oynasining **Zanjir** sahifasida chiqadi.

Pro Tools'da insert slotlari 10 ta. Agar slot yetmasa, probe'larni faqat eng muhim plaginlar orasiga qo'ying. Ikki probe orasidagi barcha plaginlar bitta bosqich sifatida baholanadi. Cubase, Ableton, Logic, FL Studio va Studio One'da slotlar soni bunday cheklovga ega emas.

**Guruh.** Bitta trekdagi probe'lar va monitor bir guruhda bo'lishi kerak. "Avto" rejimida guruh trek nomidan olinadi (Cubase, Logic, Pro Tools va Studio One nomni beradi). Agar DAW trek nomini bermasa, hamma nusxalarda bir xil "Guruh N" ni tanlang.

## UAD-2 bilan ishlash

- AZ Monitoring CPU'da (native) ishlaydi va UAD-2 DSP resursini band qilmaydi.
- Probe UAD plaginining chiqishini o'lchaydi. U plaginning ichki sozlamalarini emas, ovozga qilgan real ta'sirini baholaydi.
- Analog emulyatsiyalar (1176, LA-2A, Pultec va boshqalar) nominal darajada eng to'g'ri ishlaydi: taxminan −18 dBFS RMS ≈ 0 VU. Probe kirish darajasi −30…−10 dBFS oralig'idan chiqsa, ogohlantiradi.
- Ichki overload ham tekshiriladi. DAW float formatda 0 dBFS dan yuqori signalni saqlab qoladi, lekin keyingi analog emulyatsiya plagini shu signalni "qiziq" (haddan ortiq) qabul qiladi. Probe buni ko'rsatadi.
- Pro Tools HDX: AZ Monitoring AAX Native. DSP plaginlar orasiga native plagin qo'yilsa, Pro Tools qo'shimcha kechikish qo'shadi. Bu tahlilga ta'sir qilmaydi, faqat ovoz yo'lida bilinadi.

## Maxsus etalon (o'z standartingiz)

Ichki janr etalonlari (Pop, Hip-Hop/Rap, R&B, O'zbek estrada, Rok) faqat boshlang'ich kalibrovka. Eng aniq natija uchun **Sozlamalar → Etalon treklarni tanlash…** tugmasini bosing va o'zingiz namuna deb bilgan 3–10 ta professional trekni tanlang:
- vokal rejimi uchun alohida vokal stem yoki acapella;
- master rejimi uchun tayyor master.

AZ Monitoring ularning spektri, dinamikasi, LRA va sibilyant darajasini o'lchaydi. So'ng o'rtacha qiymatdan va ruxsat etilgan og'ishdan etalon yaratadi. Etalon diskka saqlanadi va barcha loyihalarda ishlatiladi.

## "AI" haqida — aniq aytish kerak

Hozirgi versiya ikki qismdan iborat:
1. **Standartlarga asoslangan o'lchovlar** — BS.1770, EBU R128 va Tech 3342.
2. **Qoidalarga asoslangan ekspert tizim** — tajribali mix muhandisining qoidalari asosida tashxis qo'yadi va tavsiya beradi.

Bu versiyada neyron tarmoq yo'q. Plagin sizning etalon treklaringizdan profil yaratib "o'rganadi" va tashxisni shu profilga nisbatan qo'yadi. Ichki etalonlar raqamlari va qoidalarning chegaralari boshlang'ich qiymatlar hisoblanadi. Ularni haqiqiy vokal yozuvlarida sinab, kalibrlash kerak.

Plagin boshqa plaginlarning ichki sozlamalarini (attack, ratio va boshqalar) o'qiy olmaydi: hech qaysi DAW bunga ruxsat bermaydi. Shuning uchun baho har doim signalning o'zidan olinadi.

## Yig'ish (build)

### 1-variant: GitHub Actions (eng oson)
1. Loyihani GitHub'ga yuklang.
2. **Actions → Build AZ Monitoring** avtomatik ishga tushadi.
3. Tayyor paketlarni **Artifacts** bo'limidan yuklab oling:
   - `AZ-Monitoring-Windows`: VST3 + o'rnatuvchi;
   - `AZ-Monitoring-macOS`: VST3 + AU + o'rnatuvchi.

Har bir build DSP testlari, pluginval (strictness 10) va Mac'da auval orqali avtomatik tekshiriladi.

### 2-variant: o'z kompyuteringizda
Kerakli dasturlar: CMake 3.22+ va kompilyator (Windows: Visual Studio 2022; Mac: Xcode 14+).
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release        # DSP testlari
```
JUCE 8 avtomatik yuklab olinadi. Tayyor plaginlar `build/AZMonitoring_artefacts/Release/` papkasida bo'ladi.

### Pro Tools (AAX)
1. Avid Developer akkauntidan **AAX SDK** ni yuklab oling. Akkaunt bepul.
2. Loyihani `-DAAX_SDK_PATH=/path/to/aax-sdk` bilan yig'ing. GitHub Actions'da buning o'rniga `AAX_SDK_URL` secret'ini qo'shing.
3. Oddiy Pro Tools'da ishlashi uchun AAX plagin **PACE (iLok) wraptool** bilan imzolanishi shart. Buning uchun Avid orqali PACE bilan bog'laning, dasturchilar uchun bepul. Imzolanmagan AAX faqat **Pro Tools Developer** versiyasida yuklanadi.

### macOS imzosi
CI'dan chiqqan build imzolanmagan bo'ladi. `install-mac.command` karantin belgisini olib tashlaydi. Tijorat tarqatish uchun Apple Developer ID imzosi va notarizatsiya kerak (yiliga 99 USD).

## O'rnatish
- **Windows:** `install-windows.bat` ni "Run as administrator" bilan ishga tushiring. VST3 `C:\Program Files\Common Files\VST3` papkasiga, AAX esa Avid papkasiga ko'chiriladi.
- **macOS:** `install-mac.command` ni ishga tushiring. VST3 va AU `~/Library/Audio/Plug-Ins` papkasiga ko'chiriladi.
- DAW'ni qayta ishga tushiring va plaginlarni qayta skanerlang. Plagin **Fx › Analyzer** bo'limida, ishlab chiqaruvchi nomi **Azimxodjayev Music**.

## Loyiha tuzilmasi
```
Source/
  dsp/Analyzer.*         BS.1770 loudness, True Peak, LRA, spektr, sibilyant (lock-free)
  engine/Profiles.*      Janr etalonlari, standartlar, maxsus etalon yaratish
  engine/Diagnosis.*     Tashxis qoidalari va zanjir bahosi
  engine/ProbeRegistry.* Probe va Monitor o'rtasidagi aloqa (bir jarayon ichida)
  gui/                   Interfeys: Tahlil, Rack · Zanjir, Hisobot, Sozlamalar, Probe, Plagin qo'shish
  rack/Rack.*            AZ Rack: plagin yuklash, PDC, delta, gain reduction o'lchovi, parametr tavsiyalari, katalog
  PluginProcessor.*      Plagin (bit-transparent, 0 latency)
tests/DspTests.cpp       EBU Tech 3341/3342 testlari
tests/Snapshot.cpp       Rack integratsion sinovi (haqiqiy VST3 plaginlar bilan) va interfeys suratlari
tests/testplugins/       Sinov uchun AZTest Comp va AZTest EQ plaginlari
```

## Keyingi qadamlar (v0.3+)
- Probe'lar (DAW insertlari) uchun ham delta tinglash.
- Mashhur plaginlar (1176, LA-2A, Pro-Q va boshqalar) uchun aniq parametr xaritalari.
- Haqiqiy vokal yozuvlari to'plamida etalonlar va chegaralarni kalibrlash.
- Professional miks qilingan stemlar to'plamida o'qitilgan ML modeli. U qoidalar tizimini to'ldiradi.
- Sibilyant va muammo nuqtalarini DAW'da belgi (marker) sifatida eksport qilish.

© Azimxodjayev Music
