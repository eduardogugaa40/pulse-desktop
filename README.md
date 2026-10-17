# Pulse Desktop

Base experimental de uma DAW open source para Windows, Linux e macOS.
Implementação independente: não contém código, plugins ou recursos do FL Studio.

**Estado: código inicial, ainda não compilado nem testado em execução.**
Não há instalador ou release. O protótipo web não faz parte deste projeto.

## Implementado no código
Janela nativa JUCE; seleção de arquivos MP3/WAV; até oito pistas reproduzidas
simultaneamente a partir do início; leitura antecipada em disco; conversão de
taxa de amostragem; play/stop; nomes e barras de duração das fontes.
As barras ainda não representam formas de onda. Há atenuação fixa para a soma
de oito pistas; ainda não há controles de mixagem. Início das pistas ainda usa
transportes individuais e precisa de transporte comum para precisão de amostra.

## Próximos marcos
1. Compilar e validar importação e áudio real nos três sistemas.
2. Modelo de projeto, transporte comum, clipes, desfazer/refazer e salvamento.
3. Biblioteca lateral, ondas em cache, mover/aparar/dividir clipes e mixer.
4. BPM, grade musical, metrônomo, loop e exportação WAV.
5. Efeitos internos; depois hospedagem de plugins, automação e latência.

## Compilação
Requer Git, CMake 3.22+ e compilador C++17 (MSVC no Windows, Clang/Xcode no
macOS ou GCC/Clang no Linux). CMake baixa JUCE 8.0.6 do repositório oficial.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
```

No Ubuntu, instale também as dependências listadas no workflow de CI.
O aplicativo é gerado em `build/Pulse_artefacts/Release/`.
O workflow de GitHub Actions prepara builds nos três sistemas; nenhum resultado
de CI foi obtido ainda. Builds automatizados não substituem testes de áudio real.

## Licença e distribuição
AGPL-3.0-only para o código original; JUCE sob sua opção AGPLv3.
Consulte LICENSE e LICENSE.md e preserve os avisos das dependências. Antes de
releases, revisar e incluir os avisos das dependências usadas.
Uma distribuição que não cumpra a opção AGPL do JUCE pode exigir licença comercial.

## Leveza
Meta ainda não medida. Medir memória, CPU, tempo de abertura e falhas de áudio
com projetos equivalentes antes de comparar com outras DAWs.
