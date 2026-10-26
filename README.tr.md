# Raspberry Pi Pico 2 W için DS18B20 Klon Dedektörü

[English](README.md) | **Türkçe**

DS18B20 sıcaklık sensörlerinizin **orijinal** (Maxim / Analog Devices) mi yoksa **klon/sahte** (Çin çipi) mi olduğunu Raspberry Pi Pico 2 W ile öğrenin – bilgisayar gerekmez.

Sensörü takın, kart üzerindeki LED sonucu söylesin:

| LED | Anlamı |
|---|---|
| **Sürekli YANIK** | Takılı tüm sensörler orijinal DS18B20 gibi davrandı |
| **Yanıp sönüyor (2 Hz, sürekli)** | En az bir sensör **klon** |
| Saniyede bir çift yanıp sönme | Kararsız (parazit besleme ya da sıcaklık testi tamamlanamadı) |
| 2 saniyede bir kısa yanıp sönme | Bekleme – sensör takılı değil |
| Çok hızlı titreme | Test sürüyor |

USB seri konsolda ayrıntılı rapor ve etkileşimli test menüsü de vardır.

> **Emeği geçenler.** Bu proje, **Chris Petrich**'in yazdığı
> [`discover-classify_fake_DS18B20.ino`](https://github.com/cpetrich/counterfeit_DS18B20)
> Arduino taslağının C / Pico SDK'ya taşınmış halidir. Tüm test yöntemleri,
> eşik değerleri ve sensör aile sınıflandırması, onun DS18B20 klonları üzerine
> yıllarca yaptığı araştırmanın ürünüdür. Ayrıntılı yazısını mutlaka okuyun:
> **https://github.com/cpetrich/counterfeit_DS18B20**. Beklenmedik davranan bir
> sensör bulursanız lütfen oraya bildirin.

---

## Özellikler

- **Pico SDK 2.3.1**, Pico 2 W (RP2350); resmi *Raspberry Pi Pico* VS Code eklentisiyle doğrudan derlenir.
- **8 bağımsız 1-Wire hattı**: GP28, GP27, GP26, GP22, GP21, GP20, GP19, GP18. Sensörleri bunlardan herhangi birine takabilirsiniz; her pine bir veya birden fazla sensör.
- **Bağımsız (PC'siz) mod**: hatlar her saniye taranır; sensör seti değişince güvenli test otomatik çalışır ve LED sonucu gösterir.
- **Tak-çıkar**: sensörleri art arda değiştirip LED'e bakın.
- Orijinal taslağın dört testi, mantığı değiştirilmeden:
  0. Sensörleri listele
  1. Alarm fonksiyonu testi (belgelenmiş komutlar)
  2. Klon tespiti – güvenli test, yalnızca belgelenmiş komutlar (**otomatik çalışır**)
  3. Klon ailesini sınıflandır – agresif, belgelenmemiş komutlar (onay ister)
- Her sensör için sonuç, sapma sayısı ve sıcaklık içeren özet tablo.
- Harici kütüphanesiz, temiz bir 1-Wire sürücüsü (AN126/AN187); strong pull-up desteği var, kesmeler yalnızca kısa süre kapatılır, USB akıcı kalır.
- Pico 2 W'da LED, RP2350'ye değil CYW43 kablosuz çipe bağlıdır. LED **core 1** üzerinden sürülür; böylece kablosuz sürücü core 0'daki 1-Wire zamanlamasını bozmaz.
- GitHub Actions her push'ta `.uf2` derler, sürümlere (release) ekler.

## Donanım

- Raspberry Pi Pico 2 W
- Test edilecek DS18B20 sensör(ler) (TO-92 veya su geçirmez prob)
- **Kullandığınız her hat pini için 1 adet 4.7 kΩ direnç** (data → 3V3)
- Breadboard ve jumper kablolar

### Bağlantı

```
 Pico 2 W                                   DS18B20 (düz yüzü size bakarken)
                                            ┌─────────┐
 3V3(OUT) pin 36 ───────┬───────────────────│ 3 VDD   │
                        │                   │         │
                      [4.7k]                │         │
                        │                   │         │
 GP28     pin 34 ───────┴───────────────────│ 2 DQ    │
                                            │         │
 GND      pin 38 ───────────────────────────│ 1 GND   │
                                            └─────────┘
 Su geçirmez prob: kırmızı = VDD, sarı (veya beyaz) = DQ, siyah = GND
```

Diğer hat pinleri de aynı şekilde kullanılır – her biri **kendi** 4.7 kΩ pull-up direnciyle 3V3'e:

| GPIO | Fiziksel pin | | GPIO | Fiziksel pin |
|---|---|---|---|---|
| GP28 | 34 | | GP21 | 27 |
| GP27 | 32 | | GP20 | 26 |
| GP26 | 31 | | GP19 | 25 |
| GP22 | 29 | | GP18 | 24 |

3V3(OUT) = pin 36, GND = pin 23, 28, 33 veya 38.

Notlar:

- Sensörleri **3V3**'ten besleyin; asla 5 V / VBUS / VSYS kullanmayın (GP26–GP28 ADC pinleridir, 5 V toleranslı değildir).
- **Parazit besleme kullanmayın** (VDD'yi GND'ye bağlamak). Bazı kontroller sensörün harici beslenmesini gerektirir; yazılım bu durumda *kararsız* der.
- RP2350'nin ~50 kΩ'luk dahili pull-up'ı yalnızca boştaki pinler havada kalmasın diye açıktır. Harici 4.7 kΩ direncin **yerini tutmaz**.
- GP23, GP24, GP25 ve GP29 kablosuz çip tarafından kullanılır, bu yüzden listede yoktur.

## Firmware'i edinme

### Seçenek A – indir

`ds18b20_clone_detector.uf2` dosyasını [Releases](../../releases) sayfasından (ya da *Actions* sekmesi → son çalışma → *Artifacts*) indirin.

### Seçenek B – VS Code ile derle (önerilen)

1. VS Code ve **Raspberry Pi Pico** eklentisini (`raspberry-pi.raspberry-pi-pico`) kurun.
2. Bu depoyu klonlayın.
3. Pico kenar çubuğunda **Import Project** seçin, klonlanan klasörü gösterin, **SDK 2.3.1** seçip onaylayın. Eklenti gerekirse SDK, derleyici ve picotool'u indirir.
4. Alttaki durum çubuğunda **Compile** düğmesine basın. Firmware `build/ds18b20_clone_detector.uf2` olarak oluşur.

### Seçenek C – komut satırı

```bash
export PICO_SDK_PATH=/pico-sdk/yolu   # 2.3.1, submodule'lar dahil
cmake -S . -B build -DPICO_BOARD=pico2_w
cmake --build build -j
```

## Yükleme

1. **BOOTSEL** düğmesine basılı tutarak Pico 2 W'u USB'ye takın. `RP2350` adlı bir sürücü görünür.
2. `ds18b20_clone_detector.uf2` dosyasını bu sürücüye kopyalayın. Kart yeniden başlar ve çalışır.

(Ya da VS Code'da kart BOOTSEL modundayken **Run** düğmesine basın.)

## Kullanım

### Bilgisayarsız

Kartı herhangi bir USB adaptörden besleyin, sensörleri takın, LED hızlı titrerken sensör başına ~10 sn bekleyin, sonra LED'e bakın.

### Seri konsol ile

Seri portu açın (VS Code: *Serial Monitor* eklentisi; Linux: `picocom /dev/ttyACM0`; baud hızı fark etmez). Açılış mesajı ve şu menü gelir:

```
  0. Enumerate attached sensors.                → Sensörleri listele
  1. Check temperature alarm function ...       → Alarm fonksiyonu testi
  2. Discover clones with a safe test ...       → Güvenli klon testi
  3. Classify clones with an aggressive test ...→ Agresif aile sınıflandırması
  s. Show summary of the last discover run.     → Son özet
  a. Re-run the automatic test now.             → Otomatik testi şimdi tekrarla
  h. Show this menu.                            → Menü
```

(Konsol çıktısı, orijinal projeyle karşılaştırılabilsin diye İngilizcedir.)

Her test sonunda bir özet basılır, örneğin:

```
  #  Bus   ROM                      Verdict       Dev.  Temp
  1  GP28  28-xx-xx-xx-xx-00-00-xx  GENUINE         0    23.44 oC
  2  GP27  28-xx-xx-xx-xx-xx-xx-xx  CLONE           6    23.81 oC
```

Özetin üstündeki ayrıntılı kayıt, orijinal taslakta olduğu gibi her bir kontrolü tek tek gösterir.

### Sonuçları yorumlama

- **GENUINE (orijinal)** – sensör tüm güvenli testlerde Maxim/ADI DS18B20 gibi davrandı. Not: A3 ailesi klonlar özellikle kandırmak için tasarlanmıştır; güvenli test onları ROM deseni ve polling davranışından yakalar, ancak tek kesin garanti yetkili distribütörden almaktır.
- **CLONE (klon)** – veri sayfasından veya orijinal davranıştan en az bir sapma var. Hangi aileye (A2, A3, B1, B2, C, D1, D2, E, F, G, H) ait olduğunu görmek için **3** numaralı testi kullanın. Ailelerin anlamı [orijinal README](https://github.com/cpetrich/counterfeit_DS18B20#what-families-of-ds18b20-like-chips-can-i-expect-to-encounter)'de anlatılır.
- **INCONCLUSIVE (kararsız)** – sensör parazit beslemede ya da <byte 6> mevcut sıcaklıkta doğrulanamadı. 3V3'ten besleyin, sensörü parmaklarınızla ısıtın ve `a`'ya basın.
- **COMM ERROR (iletişim hatası)** – scratchpad CRC hatası: kabloları ve pull-up direncini kontrol edin.

> ⚠️ **Test 3** belgelenmemiş komutlar gönderir. Klonların kalibrasyon parametrelerini bozma ihtimali vardır. Yalnızca test için ayırdığınız sensörlerde kullanın. Test 0–2 yalnızca belgelenmiş komutları kullanır ve EEPROM'a yazmaz.

## Yapılandırma

Tüm ayarlar [`src/config.h`](src/config.h) içindedir ve `CMakeLists.txt`'ten `target_compile_definitions(...)` ile değiştirilebilir:

| Makro | Varsayılan | Açıklama |
|---|---|---|
| `DSCD_BUS_PINS` | `28, 27, 26, 22, 21, 20, 19, 18` | 1-Wire hat pinleri |
| `DSCD_AUTO_TEST` | `1` | Takınca güvenli testi otomatik çalıştır |
| `DSCD_SCAN_INTERVAL_MS` | `1000` | Hat tarama periyodu |
| `DSCD_STABLE_SCANS` | `2` | Test öncesi gereken aynı tarama sayısı (debounce) |
| `DSCD_ENABLE_INTERNAL_PULLUP` | `1` | RP2350 dahili pull-up'larını aç |
| `DSCD_GENUINE_12BIT_MIN_MS` / `_MAX_MS` | `550` / `660` | Orijinal parçaların deneysel 12-bit dönüşüm süresi penceresi |
| `DSCD_USB_WAIT_MS` | `2500` | Açılışta seri terminal bekleme süresi |

## Proje yapısı

```
├── CMakeLists.txt          Pico SDK projesi (VS Code eklentisiyle uyumlu)
├── pico_sdk_import.cmake
├── src/
│   ├── main.c              hatlar, otomatik mod, menü, özet
│   ├── config.h            derleme zamanı ayarları
│   ├── onewire.c/.h        yazılımsal 1-Wire master (çoklu hat)
│   ├── ds18b20_tests.c/.h  orijinal testlerin portu
│   └── status_led.c/.h     core 1 üzerinde LED desenleri (CYW43)
├── .github/workflows/      CI derleme + release
├── LICENSE                 Apache Lisansı 2.0
└── NOTICE                  atıf bilgisi
```

## Orijinal taslaktan farklar

- Arduino + OneWire kütüphanesi yerine Pico SDK ve kendi 1-Wire sürücümüz.
- Çoklu hat; LED ile sonuç veren bağımsız otomatik mod; özet tablo.
- Dönüşüm süreleri mikrosaniye hassasiyetinde ölçülür.
- Alarm testi: 0 °C altında işaret doğru tam sayı sıcaklık.
- Bunların dışında test mantığı ve tüm eşikler aynıdır. Değiştirilen her kaynak dosyanın başlığında yapılan değişiklikler yazılıdır (Apache-2.0 §4(b)).

## Sorumluluk reddi

Eğitim amaçlıdır. Orijinal yazarın dediği gibi: testlerin 1-Wire sensörlere bariz ya da fark edilmeyen şekilde kalıcı zarar verebileceği durumlar olabilir. Yazılım "OLDUĞU GİBİ", hiçbir garanti olmadan sunulur – lisansa bakın.

## Lisans

[Apache Lisansı 2.0](LICENSE).

- Orijinal taslak ve araştırma: Copyright 2024 **Chris Petrich** – https://github.com/cpetrich/counterfeit_DS18B20
- Pico SDK portu: Copyright 2026 pico2w-ds18b20-clone-detector katkıda bulunanları

Atıf ayrıntıları için [NOTICE](NOTICE) dosyasına bakın.
