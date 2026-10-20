# Pulse Desktop

Base experimental de uma DAW open source para Windows, Linux e macOS.
Implementação independente: não contém código, plugins ou recursos do FL Studio.

**Estado: aplicativo inicial, ainda sem salvamento de projetos.**
A versão anterior compilou em Windows, Linux e macOS no GitHub Actions
([execução 39358527688](https://github.com/eduardogugaa40/pulse-desktop/actions/runs/39358527688)).
O sequenciador novo ainda precisa de validação de build e áudio real. Não há
instalador ou release. O protótipo web não faz parte deste projeto.

## Implementado no código
Janela nativa JUCE; seleção de arquivos MP3/WAV; até oito pistas reproduzidas
simultaneamente a partir do início; leitura antecipada em disco; conversão de
taxa de amostragem; play/pause/stop; busca de posição comum; volume, mute e
solo por pista; BPM e marcações de compasso 4/4; nomes e barras de duração.
Um Channel Rack inicial oferece um bumbo sintetizado e 16 passos. A Playlist
permite ligar/desligar instâncias desse pattern em oito compassos; Play e BPM
controlam sua geração de áudio. O pattern percorre os oito compassos em loop.
O áudio importado não é esticado nem sincronizado automaticamente ao andamento.
As barras ainda não representam formas de onda. Há atenuação fixa para a soma
de pistas antes do ganho individual. Início das pistas ainda usa
transportes individuais e precisa de transporte comum para precisão de amostra.

## Próximos marcos
1. Validar importação, sequenciamento e áudio audível nos três sistemas.
2. Modelo de projeto, transporte comum, clipes, desfazer/refazer e salvamento.
3. Biblioteca lateral, ondas em cache e mover/aparar/dividir clipes.
4. Metrônomo, loop, ajuste de áudio ao BPM e exportação WAV.
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
O workflow de GitHub Actions compila nos três sistemas. Confira a execução
mais recente para o estado deste código. Builds não substituem testes de áudio real.

## Licença e distribuição
AGPL-3.0-only para o código original; JUCE sob sua opção AGPLv3.
Consulte LICENSE e LICENSE.md e preserve os avisos das dependências. Antes de
releases, revisar e incluir os avisos das dependências usadas.
Uma distribuição que não cumpra a opção AGPL do JUCE pode exigir licença comercial.

## Leveza
Meta ainda não medida. Medir memória, CPU, tempo de abertura e falhas de áudio
com projetos equivalentes antes de comparar com outras DAWs.
