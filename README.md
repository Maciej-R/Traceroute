# Traceroute
Program wypisuje trasy pakietów do hosta docelowego, wykorzystuje TTL z nagłówka IP i bazuje na odpowiedziach ICMP TIME_EXCEEDED z węzłów sieciowych po drodze

### format wyjścia ``<#> <IP> <czas [ms]>``
•    <#> to aktualnie ustawiony TTL
•    <IP> to adres węzła od którego otrzymaliśmy odpowiedź, jeżeli odpowiedzi przychodzą tylko z jednego adresu to wypisany będzie tylko raz, a w przeciwnym wypadku przez każdym czasie podany będzie nadawca pakietów zwrotnego
•    <czas> to czas między wysłaniem, a odebraniem pakietu, powtórzony będzie kilkukrotnie w zależności od ilości zadanych próba na dany (TTL domyślnie 3)
### Opcje:
•    n – liczba prób przy stałym TTL, domyślnie 3
•    l – długość danych wysyłanych razem z pakietem ICMP, w bajtach, domyślnie 20
•    t – timeout na odbieranie pakietu, w sekundach, domyślnie 2
•    d – opóźnienie między wysyłaniem kolejnych pakietów, w sekundach, domyślnie 1
•    m – maksymalne TTL, domyślnie 64

Program tcptraceroute udostępnia tą samą funkcjonalność, ale wykorzystuje pakiety TCP SYN zamiast ICMP ECHO_REQUEST

### Instrukcja:

``` g++ main.cpp -o tracert; sudo ./tracert <address>```
```g++ tcpmain.cpp -o tcptracert; sudo ./tcptracert <address>```