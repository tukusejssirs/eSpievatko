# Zakladny prierez programami
Linux = Ubuntu
Apache2
PostgreSQL
PHP
Drupal
Musescore
xml2chordpro
ChordPro PHP (s PmWiki)

# Samostatny popis programov
## Linux
Kedze som niekolkorocnym pouzivatelom Linuxu a sucasne zavrhovatelom Micro$oftu, Linux bol pre mna automatickou volbou. Vyskusal som rozne distribucie, na Ubuntu sa citim najviac doma (hoci mi niektore veci na nom prekazaju), preto som sa rozhodol pre tuto distribuciu.

Preferujem vsak aktualnu stable verziu/release pred LTS, no ci budem mat cas wj na jej aktualizaciu dvakrat rozne, to je otazka.

## Apache2
### Dependencie

## PostgreSQL
Na vyske som v nom pracoval, a teda ho najlepsie poznam. A zda sa mi, ze je open-source, co je pre mna vyhoda

### Dependencie

### Databazy
Podla vsetkeho, potrebujeme dve, mozbo tri databazy:
* jednu pre Drupal,
* jednu pre metadata skladieb,
* a mozno jednu pre prihlasovacie udaje (ak sa to neulozi do Drupal db, prip. ak vobec bude prihlasovanie na stranke.

## PHP
Potrebuju ho viacere zo zak,adnych programov, vyzdvihnem vsak jeden: ChordPro PHP.

### Dependecie

## Drupal
CMS-ko, myslim si, je dnes absolutne minimum pri tvorbe webstranok. Netvrdim, da sa bez neho zaobist, no mnoho veci s nim  clovek nemusi pracne naprogramovat. No framework je uz dla mna privela na takyto maly projekt.

### Dependecie

## Musescore
Musescore je open-source program urceny na vytvaranie notovych zapisov. Je absolutnou nutnostou pre tuto stranku, aby vsetky zapisy boli zapisane v tom programe. Samotny zapis sa vsak uskutocnuje na klientskych pocitacoch. Na serveri ho vsak potrebujeme kvoli exportu `.mscz` do `.xml` (MusicXML), ktory nasledne dalej spracujeme.

### Dependencie

## xml2chordpro
Aktualne, tento treba este doprogramovat. Bude urceny na konverziu MusicXML `.xml` suborov do suborov ChordPro `.cho`. V tomto formate by som chcel ukladat dva druhy suborov: klasicke ChordPro, teda text piesne s akordami, a iba samotny text.

### Dependencie
`python3`

## ChordPro PHP (s PmWiki)
Tento je urceny na koverziu `.cho` na `pretty-printed` text s akordami (prip. aj iba samotny text). Je to vlastne PHP skript a formatovanie upravuje `.css` subor. Rad by som sa nejak`vymanil z nadvlady` PmWiki, aby to nebol take robustne.

### Dependecie

# Databaza s metadatami skladieb
Databaza by mala obsahovat nasledovne metaudaje skladby:
* nazov,
* skladatel,
* textar,
* prekladatel - cudzojazycneho textu do slovenciny,
* zaner,
* album,
* interpret,
* liturgicka vhodnost - ci je skladbu vhodne pouzit v liturgii (Svata Omsa, Liturgia hodin); bud 'nie' alebo bodkociarkou oddelovany zoznam casti liturgii, kedy je vhodne danu skladbu pouzit (uvod, Kyrie, Gloria, vers pred evanjeliom, Kredo, obetovanie, Sanctus, Pater noster, Agnus Dei, prijimanie, podakovanie po prijimani, zaver, ...)
* rok kompozicie
* rok zlozenia textu
* rok vydania skladby/albumu
* originalna tonina (originalu)
* originalna tonina (slovenskej verzie - prekladu zahranicnej skladby)
* copyright (licencia, prip vlastnik prav)
* tempo skladby (cislo abo slovo)
* takt (3/4, 3/8, 6/8, 4/4, 1/2, ...)


# Subory skladieb
`.mscz`
`.xml`
'.cho`
`.chol` - lyrics
