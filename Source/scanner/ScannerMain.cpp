// AZ Plugin Scanner — Azimxodjayev Music
// Plaginlarni DAW'dan TASHQARIDA, alohida jarayonda tekshiradi.
// Biror plagin qulasa, faqat shu dastur yopiladi; Cubase / Ableton / Pro Tools ishlashda davom etadi.
//
// Ishlatish:  AZPluginScanner <format nomi> <fayl yoki identifikator> <natija.xml>
// Chiqish kodi: 0 = muvaffaqiyatli, 2 = format topilmadi, 3 = plagin topilmadi
#include <juce_audio_processors/juce_audio_processors.h>

int main (int argc, char* argv[])
{
    if (argc < 4)
        return 1;

    juce::ScopedJuceInitialiser_GUI init;

    const auto formatName = juce::String::fromUTF8 (argv[1]);
    const auto identifier = juce::String::fromUTF8 (argv[2]);
    const juce::File outFile (juce::String::fromUTF8 (argv[3]));

    juce::AudioPluginFormatManager formats;
    formats.addDefaultFormats();

    juce::AudioPluginFormat* format = nullptr;
    for (int i = 0; i < formats.getNumFormats(); ++i)
        if (formats.getFormat (i)->getName() == formatName)
            format = formats.getFormat (i);
    if (format == nullptr)
        return 2;

    juce::OwnedArray<juce::PluginDescription> found;
    format->findAllTypesForFile (found, identifier);

    juce::XmlElement root ("AZSCAN");
    for (auto* d : found)
        if (auto xml = d->createXml())
            root.addChildElement (xml.release());

    if (! root.writeTo (outFile))
        return 4;
    return found.isEmpty() ? 3 : 0;
}
