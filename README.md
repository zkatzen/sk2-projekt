# sk2-projekt

## Projekt na przedmiot Sieci Komputerowe 2

#### Uruchomienie serwera:
- Gotowy skrypt *compile_tcp.sh*
- Użycie pliku *makefile* a następnie uruchomienie programu poleceniem: *./tcp_server 12345 54321* gdzie liczby 12345 oraz 54321 to numery portów

#### Połączenie klienta z serwerem:
- Po wprowadzeniu adresu IP (domyślnie - localhost) oraz numerów portów należy wcisnąć przycisk **Connect!**

#### Dodanie piosenki do playlisty:
- W polu tekstowym z domyślną treścią *... .wav filename ...* można wpisać ręcznie nazwę pliku w formacie .wav jeśli znajduje sie on w tym samym folderze co plik wykonywalny lub skorzystać z przyciksu **Select file...** który otworzy eksplorator plików
- Po wprowadzeniu nazwy utworu należy wcisnąć przycisk **Load file!** który spowoduje sprawdzenie czy plik w rzeczywistości istnieje oraz załadowanie wybrangeo utworu do bufora
- Po udanym załadowaniu pliku należy wcisnąć przycisk **send!**, spowoduje to wysłanie pliku do serwera

#### Uruchomienie/zatrzymanie playlisty
- Jeśli do serwera zostały wysłane jakieś pliki, aby uruchomić playlistę należy kliknąć przycisk **FIRE!**
- Do zatrzymania uruchomionej playlisty należy użyć przycisku **STOP**

#### Inne operacje na playliście
- Aby przejść do następnej piosenki należy klinkąć **NEXT SONG**
- Aby zmienić kolejność playlisty (UWAGA: nie może zostać zmieniona kolejność aktualnie odtwarzanego utworu) należy użyć przycisków oznaczonych **UP** oraz **DOWN**
- Aby usunąć piosenkę z playlisty należy klinkąć ptrzycisk **DELETE**, nie można usunąć aktualnie odtwarzanego pliku
