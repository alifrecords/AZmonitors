# Monitor Match — Azimxodjayev Music

Monitor emulyatori: KRK Rokit 6 yoki ADAM A8X monitorlaringizda ovozni **Focal Shape 65, Genelec 8340A, Yamaha HS8, Yamaha NS-10M** yoki ideal tekis monitordagidek eshitish uchun insert plagin.
Formatlar: **VST3** (Windows, macOS), **AU** (macOS — Logic, GarageBand), **Standalone**.

## Qanday ishlaydi

```
Korreksiya = Target monitor egri chizig'i − Sizning monitoringiz egri chizig'i
```

Natija FIR filtr sifatida qo'llanadi:

| Rejim | Kechikish (48 kHz) | Qachon |
|---|---|---|
| **Linear phase (mastering)** | ~170 ms (DAW avtomatik kompensatsiya qiladi) | Svideniya, mastering, tinglash |
| **Zero latency (aranjirovka)** | 0 ms | Yozish, MIDI chalish, aranjirovka |

Boshqaruvlar:
- **Emulyatsiya** — korreksiya kuchi (0–100 %).
- **Bass korreksiya** — 250 Hz dan pastdagi korreksiya kuchi alohida.
- **Maks. boost** — ko'tarish chegarasi. Dinamik o'zi chiqara olmaydigan chastotani (masalan, KRK 6 da 35 Hz dan past) plagin ko'tarmaydi, woofer himoyalangan.
- **Chiqish** — chiqish balandligi.
- **Auto-gain** — target almashganda balandlik o'zgarmasligi uchun avtomatik tenglash (aks holda "balandroq = yaxshiroq" aldovi bo'ladi).
- **A / B** — ikki target monitorni bir zumda solishtirish. Faol slotni tanlab, pastdagi kartani bosing.
- **ACTIVE / BYPASS** — DAW ning o'z bypass tugmasi ham shunga ulangan; bypass paytida kechikish o'zgarmaydi, "sakrash" bo'lmaydi.

## Aniqlik haqida (muhim)

Ichki profillar ishlab chiqaruvchi grafiklari va mustaqil o'lchovlar asosidagi **taxminiy** egri chiziqlar. Ular monitor xarakterini to'g'ri beradi, lekin sizning xonangizni bilmaydi. Haqiqiy aniqlik uchun:

1. Bepul **REW** (Room EQ Wizard) va o'lchov mikrofonini (masalan, Behringer ECM8000, miniDSP UMIK-1) oling.
2. Tinglash nuqtasida har bir monitorni o'lchang.
3. REW: *File → Export → Export measurement as text*.
4. Plaginda **"O'lchov faylini yuklash"** tugmasini bosing. Endi manba = sizning monitoringiz + xonangiz, va xona muammolari ham to'g'rilanadi.

O'lchov loyiha bilan birga saqlanadi.

## O'rnatish

### Tayyor fayllarni olish (kompilyatsiya kerak emas)
1. https://github.com/alifrecords/AZmonitors/actions sahifasini oching.
2. **monitor-match** branch'idagi eng oxirgi yashil ✓ "Build Monitor Match" ishga tushirishini bosing.
3. Pastdagi **Artifacts** bo'limidan `MonitorMatch-Windows` yoki `MonitorMatch-macOS` ni yuklab oling.

Qayta yig'ish kerak bo'lsa: Actions → "Build Monitor Match" → **Run workflow** → branch: `monitor-match`.

### Windows
- `Monitor Match.vst3` papkasini `C:\Program Files\Common Files\VST3\` ga ko'chiring.

### macOS
- `Monitor Match.vst3` → `~/Library/Audio/Plug-Ins/VST3/`
- `Monitor Match.component` → `~/Library/Audio/Plug-Ins/Components/`
- Imzosiz plagin bo'lgani uchun Terminalda bir marta:
  `xattr -cr ~/Library/Audio/Plug-Ins/VST3/"Monitor Match.vst3" ~/Library/Audio/Plug-Ins/Components/"Monitor Match.component"`

## DAW larda ishlatish

**Plagin faqat eshitish uchun — tayyor trekka yozilmasligi kerak.**

- **Cubase / Nuendo (eng yaxshi usul):** *Studio → Studio Setup → Plug-in Manager* da qayta skanerlang. So'ng **Control Room → Main → Inserts** ga qo'ying. Control Room faqat monitorga boradi, Export Audio Mixdown ga hech qachon tushmaydi. Cubase Elements/AI da Control Room yo'q — Stereo Out ning oxirgi insertiga qo'ying va eksportdan oldin o'chiring.
- **Studio One:** Main bus → oxirgi insert. Eksportdan oldin bypass.
- **FL Studio:** Master mixer → oxirgi slot. Eksportdan oldin o'chiring.
- **Ableton Live:** Master track → zanjir oxiri. Eksportdan oldin o'chiring.
- **Logic Pro (AU):** Stereo Out → oxirgi insert. Eksportdan oldin bypass.
- **Reaper:** Master → *Monitoring FX* ga qo'ying (render ga tushmaydi).

Aranjirovka va yozish paytida **Zero latency** rejimiga o'ting, svideniya va mastering paytida **Linear phase** ga qaytaring.

## O'zingiz kompilyatsiya qilish

Kerak: CMake 3.22+, Windows'da Visual Studio 2022, macOS'da Xcode.

```
cmake -S . -B build
cmake --build build --config Release
```

JUCE 8.0.15 avtomatik yuklab olinadi. Profillarni o'zgartirish: `Source/Profiles.h`.

## Litsenziya eslatmasi

JUCE 8 — AGPLv3 yoki tijoriy litsenziya. Plaginni yopiq kod bilan sotmoqchi bo'lsangiz, JUCE litsenziyasini oling (yillik daromad $50k gacha — bepul "Starter").
