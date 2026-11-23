#pragma once
#include <cstddef>
#include <string>

namespace i18n {
enum class Lang { EN, PT };
inline const char* code(Lang lang) { return lang == Lang::PT ? "pt" : "en"; }

// Texto do app. Títulos, nomes e metadados do servidor continuam sendo dados.
#define TUBE360_TEXT(X) \
  X(None, "", "") \
  X(ServerOriginal, "Server · original", "Servidor · original") \
  X(ServerConverted, "Server · converted", "Servidor · convertido") \
  X(ConsoleVideoError, "Could not open the YouTube stream on this console.", "O console não conseguiu abrir o fluxo do YouTube.") \
  X(Console, "Console", "Console") \
  X(ServerMode, "Server", "Servidor") \
  X(Automatic, "Automatic", "Automático") \
  X(Home, "Home", "Início") \
  X(Search, "Search", "Buscar") \
  X(Phone, "Phone", "Celular") \
  X(Settings, "Settings", "Configurações") \
  X(Exit, "Exit", "Sair") \
  X(All, "All", "Tudo") \
  X(ForYou, "For you", "Para você") \
  X(Subscriptions, "Subscriptions", "Inscrições") \
  X(ContinueWatching, "Continue watching", "Continuar assistindo") \
  X(WatchLater, "Watch later", "Assistir mais tarde") \
  X(Music, "Music", "Música") \
  X(Football, "Football", "Futebol") \
  X(Games, "Games", "Games") \
  X(Podcasts, "Podcasts", "Podcasts") \
  X(News, "News", "Notícias") \
  X(MusicQuery, "popular music hits", "músicas brasileiras mais tocadas") \
  X(FootballQuery, "football match highlights", "melhores momentos futebol brasileiro") \
  X(GamesQuery, "gameplay gaming", "gameplay pt br") \
  X(PodcastsQuery, "podcast highlights", "cortes podcast") \
  X(NewsQuery, "today world news", "notícias de hoje brasil") \
  X(Keyboard1, "abcdefghij", "abcdefghij") \
  X(Keyboard2, "klmnopqrst", "klmnopqrst") \
  X(Keyboard3, "uvwxyz&-'.", "uvwxyzç-'.") \
  X(Keyboard4, "1234567890", "1234567890") \
  X(Refresh, "Refresh", "Atualizar") \
  X(Refreshing, "Finding new videos…", "Buscando vídeos novos…") \
  X(LoadingVideos, "Loading videos…", "Carregando vídeos…") \
  X(ServerUnavailable, "Could not reach the server", "Não deu para falar com o servidor") \
  X(NoResponse, "No response from the server", "Sem resposta do servidor") \
  X(BadResponse, "Could not load this list", "Não deu para carregar esta lista") \
  X(RetryServer, "Start the server and press X to try again.", "Ligue o servidor e aperte X para tentar de novo.") \
  X(Empty, "Nothing found.", "Nada encontrado.") \
  X(PageError, "Could not load more videos. Press X to refresh.", "Não deu para carregar mais vídeos. Aperte X para atualizar.") \
  X(Live, "LIVE", "AO VIVO") \
  X(PhonePrefix, "Phone: ", "Celular: ") \
  X(AccountPrefix, "Account: ", "Conta: ") \
  X(SearchPrompt, "What do you want to watch?", "O que você quer assistir?") \
  X(RecentSearches, "Recent searches", "Buscas recentes") \
  X(RecentEmpty, "Your searches will appear here.", "O que você buscar aparece aqui.") \
  X(Type, "Type", "Digitar") \
  X(Delete, "Delete", "Apagar") \
  X(Space, "Space", "Espaço") \
  X(Back, "Back", "Voltar") \
  X(Close, "Close", "Fechar") \
  X(Resume, "Resume", "Continuar") \
  X(Pause, "Pause", "Pausar") \
  X(Original, "original", "original") \
  X(Converted, "converted", "convertido") \
  X(Q720, "720p", "720p") \
  X(Q480, "480p", "480p") \
  X(Q360, "360p", "360p") \
  X(QualityPrefix, "Quality: ", "Qualidade: ") \
  X(SoundPrefix, "Sound: ", "Som: ") \
  X(TV, "TV", "TV") \
  X(Nest, "Nest", "Nest") \
  X(SyncPrefix, "Speaker sync: ", "Sincronia da caixa: ") \
  X(Milliseconds, " ms", " ms") \
  X(TenSeconds, "10 s", "10 s") \
  X(OneMinute, "1 min", "1 min") \
  X(SpeakerFallback, "Speaker did not respond: using TV sound (X to retry)", "A caixa não respondeu: som na TV (X tenta de novo)") \
  X(RequestingVideo, "Requesting video from the server…", "Pedindo o vídeo ao servidor…") \
  X(Loading, "Loading…", "Carregando…") \
  X(VideoFailed, "Could not play this video", "Não deu para tocar este vídeo") \
  X(VideoOpenError, "The server could not open this video", "O servidor não conseguiu abrir este vídeo") \
  X(VideoClosed, "The server closed the video connection", "O servidor fechou a conexão do vídeo") \
  X(Rate, "Rate", "Avaliar") \
  X(RateTitle, "Rate this video", "Avaliar este vídeo") \
  X(Like, "Like", "Gostei") \
  X(Dislike, "Dislike", "Não gostei") \
  X(Liked, "You liked this video", "Você marcou que gostou") \
  X(Disliked, "You disliked this video", "Você marcou que não gostou") \
  X(RatingRemoved, "Rating removed", "Avaliação removida") \
  X(RateFailed, "Could not save your rating", "Não deu para salvar a avaliação") \
  X(RateSignIn, "Sign in to YouTube in Settings to rate videos", "Conecte sua conta do YouTube nas configurações para avaliar") \
  X(UpNextIn, "Up next in ", "A seguir em ") \
  X(WatchNow, "Watch now", "Assistir agora") \
  X(VideoMembers, "This video is only for channel members", "Este vídeo é só para membros do canal") \
  X(VideoPrivate, "This video is private", "Este vídeo é privado") \
  X(VideoAge, "YouTube asks to confirm your age for this video", "O YouTube pede confirmação de idade para este vídeo") \
  X(VideoGone, "This video is no longer available on YouTube", "Este vídeo não está mais disponível no YouTube") \
  X(SpeakerError, "The speaker could not start playback", "A caixa não conseguiu começar a tocar") \
  X(PhoneRequest, "Requested from your phone", "Pedido do celular") \
  X(PhoneConnected, "Phone connected: ", "Celular conectado: ") \
  X(PhoneDisconnected, "Phone disconnected", "O celular desconectou") \
  X(PhoneUpdated, "Phone connection updated", "Conexão do celular atualizada") \
  X(CastTitle, "Cast from your phone", "Transmitir do celular") \
  X(CastChoose, "In the YouTube app, tap Cast and choose ", "No app do YouTube, toque em Transmitir e escolha ") \
  X(CastPath, "Or: your profile > Settings > Watch on TV > Enter TV code", "Ou: sua foto > Configurações > Assistir na TV > Inserir código da TV") \
  X(CastLoading, "Getting the TV code… Keep the Cast receiver running.", "Buscando o código… Mantenha o receptor do servidor ligado.") \
  X(CastRemember, "The code changes every 5 minutes. Pair once and your phone remembers this TV.", "O código muda a cada 5 minutos. Pareie uma vez e o celular lembra desta TV.") \
  X(NoPhone, "No phone connected yet.", "Nenhum celular conectado ainda.") \
  X(ServerOffline, "The server is offline right now.", "Sem conexão com o servidor agora.") \
  X(SettingsIntro, "Make yourself at home.", "Do seu jeito, no seu console.") \
  X(YouTubeAccount, "YouTube account", "Conta do YouTube") \
  X(VideosPerRow, "Videos per row", "Vídeos por linha") \
  X(DefaultQuality, "Default quality", "Qualidade padrão") \
  X(Sound, "Sound", "Som") \
  X(SpeakerSync, "Speaker sync", "Sincronia da caixa") \
  X(SleepTimer, "Sleep timer", "Timer de desligar") \
  X(SleepTogether, "Also turn off", "Desligar junto") \
  X(SleepOff, "Off", "Desligado") \
  X(SleepEnd, "At end of video", "No fim do vídeo") \
  X(SleepXbox, "Xbox only", "Só o Xbox") \
  X(SleepNotebook, "Xbox and notebook", "Xbox e notebook") \
  X(SleepTVHelp, "Samsung TV turns off automatically with 'No Signal Power Off' enabled.", "A TV Samsung desliga sozinha se 'Desligamento sem sinal' estiver ligado.") \
  X(SleepWarning, "Shutting down in ", "Desligando em ") \
  X(SleepSeconds, " s", " s") \
  X(SleepAdd, "+15 min", "+15 min") \
  X(SleepShuttingDown, "Shutting down…", "Desligando…") \
  X(SleepError, "Could not update sleep timer. Try again.", "Não deu para alterar o timer. Tente de novo.") \
  X(SleepApply, "Apply", "Aplicar") \
  X(PlayerOptions, "Options", "Opções") \
  X(PlayerTimeline, "Timeline", "Trilha do vídeo") \
  X(PlayerQuality, "Quality", "Qualidade") \
  X(PlayerChannel, "Go to channel", "Ir para o canal") \
  X(PlayerPaused, "Paused", "Pausado") \
  X(PlayerSeekWait, "Seeking is available after the video starts", "Você pode pular depois que o vídeo começar") \
  X(PlayerLiveSeek, "Live broadcast: seeking is unavailable", "Transmissão ao vivo: não dá para pular") \
  X(Captions, "Captions", "Legendas") \
  X(CaptionsOn, "On", "Ligadas") \
  X(CaptionsOff, "Off", "Desligadas") \
  X(CaptionsAuto, "Automatic", "Automáticas") \
  X(CaptionsManual, "Creator captions", "Do criador") \
  X(CaptionsLoading, "Finding captions…", "Buscando legendas…") \
  X(CaptionsNone, "This video has no captions", "Este vídeo não oferece legendas") \
  X(CaptionsLiveNone, "YouTube has no captions for this live broadcast", "O YouTube não oferece legendas nesta live") \
  X(CaptionsFailed, "Could not load captions. Press A to retry.", "Não deu para carregar legendas. Aperte A para tentar de novo.") \
  X(CaptionsLanguage, "Caption language", "Idioma das legendas") \
  X(PlayerAlsoOff, "Also turn off", "Também desligar") \
  X(PlayerTimerOn, "Active", "Ativo") \
  X(CaptionsLiveClock, "Captions for this broadcast are unavailable in the player", "As legendas desta live estão indisponíveis no player") \
  X(Language, "Language", "Idioma") \
  X(English, "English", "English") \
  X(Portuguese, "Português", "Português") \
  X(SignedOut, "Signed out", "Desconectado") \
  X(SignInAction, "Press A to sign in", "A para conectar") \
  X(SignOutAction, "Press A to sign out", "A para desconectar") \
  X(SignedInAs, "Connected as ", "Conectado como ") \
  X(Pending, "Waiting for sign-in", "Aguardando login") \
  X(SavedAutomatically, "Changes are saved automatically.", "As mudanças são salvas automaticamente.") \
  X(SaveFailed, "Could not save settings on this device", "Não deu para salvar as configurações neste dispositivo") \
  X(NoSpeaker, "No speaker configured on the server", "Nenhuma caixa configurada no servidor") \
  X(SpeakerChecking, "Checking for a speaker…", "Procurando uma caixa…") \
  X(SyncHelp, "Adjust in steps of 40 ms. Positive values advance the picture.", "Ajuste em passos de 40 ms. Valores positivos adiantam a imagem.") \
  X(ChooseRow, "Select", "Escolher") \
  X(ChangeValue, "Change", "Alterar") \
  X(Open, "Open", "Abrir") \
  X(Three, "3", "3") \
  X(Four, "4", "4") \
  X(SignInTitle, "Sign in to YouTube", "Conectar ao YouTube") \
  X(SignInStep, "On your phone or computer, open", "No celular ou computador, abra") \
  X(DeviceURL, "google.com/device", "google.com/device") \
  X(EnterCode, "Enter this code", "Digite este código") \
  X(LoginWaiting, "Waiting for Google confirmation…", "Aguardando a confirmação do Google…") \
  X(LoginExpired, "Code expired. Press A to get a new one.", "O código expirou. Aperte A para gerar outro.") \
  X(AccountError, "Could not update your account. Press A to retry.", "Não deu para atualizar a conta. Aperte A para tentar de novo.") \
  X(AccountUnavailable, "Account unavailable. Press A to retry.", "Conta indisponível. Aperte A para tentar de novo.") \
  X(SignOutTitle, "Sign out of YouTube?", "Desconectar do YouTube?") \
  X(SignOutBody, "Your personal lists will be removed from this console.", "Suas listas pessoais deixarão de aparecer neste console.") \
  X(Confirm, "Confirm", "Confirmar") \
  X(Cancel, "Cancel", "Cancelar") \
  X(SignedOutToast, "Signed out of YouTube", "Conta do YouTube desconectada") \
  X(Library, "Library", "Biblioteca") \
  X(Channel, "Channel", "Canal") \
  X(GoChannel, "Go to channel", "Ir para o canal") \
  X(Subscribe, "Subscribe", "Inscrever-se") \
  X(Subscribed, "Subscribed", "Inscrito") \
  X(Videos, "Videos", "Vídeos") \
  X(Playlists, "Playlists", "Playlists") \
  X(PlayAll, "Play all", "Tocar tudo") \
  X(QuickPicks, "Quick picks", "Escolhas rápidas") \
  X(Charts, "Charts", "Paradas") \
  X(Mixes, "Mixes", "Mixes") \
  X(Songs, "Songs", "Músicas") \
  X(Albums, "Albums", "Álbuns") \
  X(Artists, "Artists", "Artistas") \
  X(Artist, "Artist", "Artista") \
  X(Album, "Album", "Álbum") \
  X(Radio, "Start radio", "Iniciar rádio") \
  X(NowPlaying, "Now playing", "Tocando agora") \
  X(Queue, "Up next", "A seguir") \
  X(NextTrack, "Next track", "Próxima faixa") \
  X(Lyrics, "Lyrics", "Letra") \
  X(NoLyrics, "Lyrics are not available for this track.", "Letra indisponível para esta faixa.") \
  X(ShowVideo, "Show video", "Mostrar vídeo") \
  X(CardMenu, "Video options", "Opções do vídeo") \
  X(AddedLater, "Saved to Watch later", "Salvo em Assistir mais tarde") \
  X(AccountRequired, "Sign in to YouTube in Settings to save and subscribe.", "Conecte sua conta em Configurações para salvar e se inscrever.") \
  X(ActionFailed, "Could not save. Press A to retry.", "Não foi possível salvar. Aperte A para tentar de novo.") \
  X(Saving, "Saving…", "Salvando…") \
  X(StartingQueue, "Preparing the queue…", "Preparando a fila…") \
  X(History, "History", "Histórico") \
  X(MusicSearch, "Search songs, albums and artists", "Buscar músicas, álbuns e artistas") \
  X(NoLive, "No live streams on this channel.", "Este canal não tem transmissões ao vivo.") \
  X(MoreLoading, "Loading the next page…", "Carregando a próxima página…") \
  X(Results, "Search results", "Resultados da busca") \
  X(AllResults, "All results", "Tudo") \
  X(MoreOptions, "Options", "Opções") \
  X(ChooseTab, "Choose a tab", "Escolher aba") \
  X(MusicIntro, "Your next favourite sound.", "Seu próximo som favorito.") \
  X(QueueLimit, "Playlists play up to 256 tracks.", "As playlists tocam até 256 faixas.") \
  X(SubscribeConfirm, "Unsubscribe from this channel?", "Cancelar a inscrição neste canal?") \
  X(ChannelLive, "Live", "Ao vivo") \
  X(MusicSpeakerFallback, "Speaker did not respond: sound on the TV.", "A caixa não respondeu: som na TV.") \
  X(BtnA, "A", "A") X(BtnB, "B", "B") X(BtnX, "X", "X") X(BtnY, "Y", "Y") \
  X(BtnView, "View", "View") X(BtnMenu, "Menu", "Menu") \
  X(BtnShoulders, "LB RB", "LB RB") X(BtnTriggers, "LT RT", "LT RT") \
  X(BtnHorizontal, "< >", "< >") X(BtnVertical, "↑ ↓", "↑ ↓")

enum class Key {
#define TEXT_KEY(k, en, pt) k,
  TUBE360_TEXT(TEXT_KEY)
#undef TEXT_KEY
  Count
};
inline constexpr const char* texts[][2] = {
#define TEXT_VALUE(k, en, pt) {en, pt},
  TUBE360_TEXT(TEXT_VALUE)
#undef TEXT_VALUE
};
#undef TUBE360_TEXT
static_assert(sizeof(texts) / sizeof(texts[0]) == (size_t)Key::Count);
inline const char* get(Lang lang, Key key) { return texts[(size_t)key][lang == Lang::PT ? 1 : 0]; }
inline Key quality(int q) { return q == 360 ? Key::Q360 : q == 480 ? Key::Q480 : Key::Q720; }

// player.hpp mantém os diagnósticos internos; só a apresentação é traduzida.
inline Key playerError(const std::string& error) {
  if (error == "console") return Key::ConsoleVideoError;
  if (error == "Sem conexão com o servidor do notebook") return Key::NoResponse;
  if (error == "O servidor não conseguiu abrir este vídeo") return Key::VideoOpenError;
  if (error == "O vídeo não chegou (o servidor fechou a conexão)") return Key::VideoClosed;
  if (error == "Vídeo só para membros do canal") return Key::VideoMembers;
  if (error == "Vídeo privado") return Key::VideoPrivate;
  if (error == "Vídeo com restrição de idade") return Key::VideoAge;
  if (error == "Vídeo indisponível no YouTube") return Key::VideoGone;
  if (error.find("Nest") != std::string::npos) return Key::SpeakerError;
  return Key::VideoOpenError;
}
}  // namespace i18n
