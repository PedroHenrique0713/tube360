#pragma once
#include <cstddef>
#include <cstdio>
#include <string>

namespace i18n {
enum class Lang { EN, PT, ES, Count };
inline const char* code(Lang lang) { return lang == Lang::PT ? "pt" : lang == Lang::ES ? "es" : "en"; }

inline Lang language(const std::string& code) { return code == "pt" ? Lang::PT : code == "es" ? Lang::ES : Lang::EN; }
inline Lang cycle(Lang lang, int direction) { return Lang((int(lang)+direction+int(Lang::Count))%int(Lang::Count)); }

inline std::string shortDate(const std::string& iso, Lang lang) {
  int year, month, day;
  if (iso.size() != 10 || std::sscanf(iso.c_str(), "%4d-%2d-%2d", &year, &month, &day) != 3 ||
      month < 1 || month > 12 || day < 1 || day > 31) return iso;
  static const char* const months[][12] = {
    {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"},
    {"jan","fev","mar","abr","mai","jun","jul","ago","set","out","nov","dez"},
    {"ene","feb","mar","abr","may","jun","jul","ago","sep","oct","nov","dic"}
  };
  char date[32];
  if (lang != Lang::EN) std::snprintf(date, sizeof(date), "%d %s %d", day, months[lang == Lang::ES ? 2 : 1][month-1], year);
  else std::snprintf(date, sizeof(date), "%s %d, %d", months[0][month-1], day, year);
  return date;
}

// Texto do app. Títulos, nomes e metadados do servidor continuam sendo dados.
#define TUBE360_TEXT(X) \
  X(None, "", "", "") \
  X(ServerOriginal, "Server · original", "Servidor · original", "Servidor · original") \
  X(ServerConverted, "Server · converted", "Servidor · convertido", "Servidor · convertido") \
  X(ConsoleVideoError, "Could not open the YouTube stream on this console.", "O console não conseguiu abrir o fluxo do YouTube.", "No se pudo abrir el video de YouTube en esta consola.") \
  X(Console, "Console", "Console", "Consola") \
  X(ServerMode, "Server", "Servidor", "Servidor") \
  X(Automatic, "Automatic", "Automático", "Automático") \
  X(Video720Fast, "720p · 60 fps", "720p · 60 fps", "720p · 60 fps") \
  X(Video1080Detail, "1080p · 30 fps", "1080p · 30 fps", "1080p · 30 fps") \
  X(VideoAdjusted, "Adjusted to keep playback smooth", "Ajustado para manter a reprodução fluida", "Ajustado para mantener una reproducción fluida") \
  X(DisplayOutput, "TV output", "Saída da TV", "Salida de TV") \
  X(Display1080Help, "For native 1080p, select 1080p (progressive) in Xbox Display Settings and use HDMI.", "Para 1080p nativo, selecione 1080p progressivo no painel do Xbox e use HDMI.", "Para 1080p nativo, selecciona 1080p progresivo en la configuración de pantalla de Xbox y usa HDMI.") \
  X(AudioTrack, "Audio", "Áudio", "Audio") \
  X(AudioPreference, "Audio preference", "Preferência de áudio", "Preferencia de audio") \
  X(AudioAlwaysOriginal, "Always original", "Sempre o original", "Siempre original") \
  X(AudioInterface, "Interface language when available", "Idioma da interface quando houver", "Idioma de la interfaz, si está disponible") \
  X(AudioOriginal, "Original", "Original", "Original") \
  X(AudioAutoDub, "automatically dubbed", "dublado automaticamente", "doblaje automático") \
  X(AudioDub, "dubbed", "dublado", "doblado") \
  X(AudioEnglish, "English", "inglês", "inglés") \
  X(AudioPortuguese, "Portuguese", "português", "portugués") \
  X(AudioSpanish, "Spanish", "espanhol", "español") \
  X(AudioFrench, "French", "francês", "francés") \
  X(AudioGerman, "German", "alemão", "alemán") \
  X(AudioJapanese, "Japanese", "japonês", "japonés") \
  X(AudioUnknown, "Audio track", "Faixa de áudio", "Pista de audio") \
  X(Home, "Home", "Início", "Inicio") \
  X(Search, "Search", "Buscar", "Buscar") \
  X(Phone, "Phone", "Celular", "Teléfono") \
  X(Settings, "Settings", "Configurações", "Configuración") \
  X(Exit, "Exit", "Sair", "Salir") \
  X(All, "All", "Tudo", "Todo") \
  X(ForYou, "For you", "Para você", "Para ti") \
  X(Subscriptions, "Subscriptions", "Inscrições", "Suscripciones") \
  X(ContinueWatching, "Continue watching", "Continuar assistindo", "Seguir viendo") \
  X(WatchLater, "Watch later", "Assistir mais tarde", "Ver más tarde") \
  X(Music, "Music", "Música", "Música") \
  X(Football, "Football", "Futebol", "Fútbol") \
  X(Games, "Games", "Games", "Videojuegos") \
  X(Podcasts, "Podcasts", "Podcasts", "Podcasts") \
  X(News, "News", "Notícias", "Noticias") \
  X(MusicQuery, "popular music hits", "músicas brasileiras mais tocadas", "éxitos de música en español") \
  X(FootballQuery, "football match highlights", "melhores momentos futebol brasileiro", "resúmenes de partidos de fútbol") \
  X(GamesQuery, "gameplay gaming", "gameplay pt br", "videojuegos en español") \
  X(PodcastsQuery, "podcast highlights", "cortes podcast", "fragmentos de podcasts en español") \
  X(NewsQuery, "today world news", "notícias de hoje brasil", "noticias de hoy") \
  X(Keyboard1, "abcdefghij", "abcdefghij", "abcdefghij") \
  X(Keyboard2, "klmnopqrst", "klmnopqrst", "klmnopqrst") \
  X(Keyboard3, "uvwxyz&-'.", "uvwxyzç-'.", "uvwxyzñ-'.") \
  X(Keyboard4, "1234567890", "1234567890", "1234567890") \
  X(Refresh, "Refresh", "Atualizar", "Actualizar") \
  X(Refreshing, "Finding new videos…", "Buscando vídeos novos…", "Buscando videos nuevos…") \
  X(LoadingVideos, "Loading videos…", "Carregando vídeos…", "Cargando videos…") \
  X(ServerUnavailable, "Could not reach the server", "Não deu para falar com o servidor", "No se pudo conectar con el servidor") \
  X(NoResponse, "No response from the server", "Sem resposta do servidor", "El servidor no responde") \
  X(BadResponse, "Could not load this list", "Não deu para carregar esta lista", "No se pudo cargar esta lista") \
  X(RetryServer, "Start the server and press X to try again.", "Ligue o servidor e aperte X para tentar de novo.", "Inicia el servidor y presiona X para volver a intentarlo.") \
  X(Empty, "Nothing found.", "Nada encontrado.", "No se encontraron resultados.") \
  X(PageError, "Could not load more videos. Press X to refresh.", "Não deu para carregar mais vídeos. Aperte X para atualizar.", "No se pudieron cargar más videos. Presiona X para actualizar.") \
  X(Live, "LIVE", "AO VIVO", "EN VIVO") \
  X(PhonePrefix, "Phone: ", "Celular: ", "Teléfono: ") \
  X(AccountPrefix, "Account: ", "Conta: ", "Cuenta: ") \
  X(SearchPrompt, "What do you want to watch?", "O que você quer assistir?", "¿Qué quieres ver?") \
  X(RecentSearches, "Recent searches", "Buscas recentes", "Búsquedas recientes") \
  X(RecentEmpty, "Your searches will appear here.", "O que você buscar aparece aqui.", "Tus búsquedas aparecerán aquí.") \
  X(Type, "Type", "Digitar", "Escribir") \
  X(Delete, "Delete", "Apagar", "Borrar") \
  X(Space, "Space", "Espaço", "Espacio") \
  X(Back, "Back", "Voltar", "Volver") \
  X(Close, "Close", "Fechar", "Cerrar") \
  X(Resume, "Resume", "Continuar", "Continuar") \
  X(Pause, "Pause", "Pausar", "Pausar") \
  X(Original, "original", "original", "original") \
  X(Converted, "converted", "convertido", "convertido") \
  X(Q720, "720p", "720p", "720p") \
  X(Q480, "480p", "480p", "480p") \
  X(Q360, "360p", "360p", "360p") \
  X(QualityPrefix, "Quality: ", "Qualidade: ", "Calidad: ") \
  X(SoundPrefix, "Sound: ", "Som: ", "Sonido: ") \
  X(TV, "TV", "TV", "TV") \
  X(Nest, "Nest", "Nest", "Nest") \
  X(SyncPrefix, "Speaker sync: ", "Sincronia da caixa: ", "Sincronización del altavoz: ") \
  X(Milliseconds, " ms", " ms", " ms") \
  X(TenSeconds, "10 s", "10 s", "10 s") \
  X(OneMinute, "1 min", "1 min", "1 min") \
  X(SpeakerFallback, "Speaker did not respond: using TV sound (X to retry)", "A caixa não respondeu: som na TV (X tenta de novo)", "El altavoz no responde: sonido en la TV (X para reintentar)") \
  X(RequestingVideo, "Requesting video from the server…", "Pedindo o vídeo ao servidor…", "Solicitando el video al servidor…") \
  X(Loading, "Loading…", "Carregando…", "Cargando…") \
  X(VideoFailed, "Could not play this video", "Não deu para tocar este vídeo", "No se pudo reproducir este video") \
  X(VideoOpenError, "The server could not open this video", "O servidor não conseguiu abrir este vídeo", "El servidor no pudo abrir este video") \
  X(VideoClosed, "The server closed the video connection", "O servidor fechou a conexão do vídeo", "El servidor cerró la conexión del video") \
  X(Rate, "Rate", "Avaliar", "Calificar") \
  X(RateTitle, "Rate this video", "Avaliar este vídeo", "Calificar este video") \
  X(Like, "Like", "Gostei", "Me gusta") \
  X(Dislike, "Dislike", "Não gostei", "No me gusta") \
  X(Liked, "You liked this video", "Você marcou que gostou", "Marcaste que te gusta este video") \
  X(Disliked, "You disliked this video", "Você marcou que não gostou", "Marcaste que no te gusta este video") \
  X(RatingRemoved, "Rating removed", "Avaliação removida", "Calificación eliminada") \
  X(RateFailed, "Could not save your rating", "Não deu para salvar a avaliação", "No se pudo guardar tu calificación") \
  X(RateSignIn, "Sign in to YouTube in Settings to rate videos", "Conecte sua conta do YouTube nas configurações para avaliar", "Inicia sesión en YouTube desde Configuración para calificar") \
  X(UpNextIn, "Up next in ", "A seguir em ", "A continuación en ") \
  X(WatchNow, "Watch now", "Assistir agora", "Ver ahora") \
  X(VideoMembers, "This video is only for channel members", "Este vídeo é só para membros do canal", "Este video es exclusivo para miembros del canal") \
  X(VideoPrivate, "This video is private", "Este vídeo é privado", "Este video es privado") \
  X(VideoAge, "YouTube asks to confirm your age for this video", "O YouTube pede confirmação de idade para este vídeo", "YouTube solicita confirmar tu edad para ver este video") \
  X(VideoGone, "This video is no longer available on YouTube", "Este vídeo não está mais disponível no YouTube", "Este video ya no está disponible en YouTube") \
  X(SpeakerError, "The speaker could not start playback", "A caixa não conseguiu começar a tocar", "El altavoz no pudo iniciar la reproducción") \
  X(PhoneRequest, "Requested from your phone", "Pedido do celular", "Solicitado desde tu teléfono") \
  X(PhoneConnected, "Phone connected: ", "Celular conectado: ", "Teléfono conectado: ") \
  X(PhoneDisconnected, "Phone disconnected", "O celular desconectou", "El teléfono se desconectó") \
  X(PhoneUpdated, "Phone connection updated", "Conexão do celular atualizada", "Conexión del teléfono actualizada") \
  X(CastTitle, "Cast from your phone", "Transmitir do celular", "Transmitir desde tu teléfono") \
  X(CastChoose, "In the YouTube app, tap Cast and choose ", "No app do YouTube, toque em Transmitir e escolha ", "En la app de YouTube, toca Transmitir y elige ") \
  X(CastPath, "Or: your profile > Settings > Watch on TV > Enter TV code", "Ou: sua foto > Configurações > Assistir na TV > Inserir código da TV", "O bien: tu perfil > Configuración > Ver en TV > Ingresar código de TV") \
  X(CastOffline, "Cast is unavailable. Check the network connection.", "Transmitir indisponível. Verifique a conexão de rede.", "Transmitir no está disponible. Revisa la conexión de red.") \
  X(CastLoading, "Getting the TV code…", "Buscando o código…", "Obteniendo el código de TV…") \
  X(CastRemember, "The code changes every 5 minutes. Pair once and your phone remembers this TV.", "O código muda a cada 5 minutos. Pareie uma vez e o celular lembra desta TV.", "El código cambia cada 5 minutos. Vincula tu teléfono una vez para recordar esta TV.") \
  X(NoPhone, "No phone connected yet.", "Nenhum celular conectado ainda.", "Todavía no hay un teléfono conectado.") \
  X(ServerOffline, "The server is offline right now.", "Sem conexão com o servidor agora.", "El servidor está desconectado.") \
  X(SettingsIntro, "Make yourself at home.", "Do seu jeito, no seu console.", "A tu gusto, en tu consola.") \
  X(YouTubeAccount, "YouTube account", "Conta do YouTube", "Cuenta de YouTube") \
  X(VideosPerRow, "Videos per row", "Vídeos por linha", "Videos por fila") \
  X(DefaultQuality, "Default quality", "Qualidade padrão", "Calidad predeterminada") \
  X(Sound, "Sound", "Som", "Sonido") \
  X(SpeakerSync, "Speaker sync", "Sincronia da caixa", "Sincronización del altavoz") \
  X(SleepTimer, "Sleep timer", "Timer de desligar", "Temporizador de apagado") \
  X(SleepTogether, "Also turn off", "Desligar junto", "Apagar también") \
  X(SleepOff, "Off", "Desligado", "Desactivado") \
  X(SleepEnd, "At end of video", "No fim do vídeo", "Al terminar el video") \
  X(SleepXbox, "Xbox only", "Só o Xbox", "Solo Xbox") \
  X(SleepNotebook, "Xbox and notebook", "Xbox e notebook", "Xbox y computadora") \
  X(SleepTVHelp, "Samsung TV turns off automatically with 'No Signal Power Off' enabled.", "A TV Samsung desliga sozinha se 'Desligamento sem sinal' estiver ligado.", "La TV Samsung se apaga sola si activas el apagado por falta de señal.") \
  X(SleepWarning, "Shutting down in ", "Desligando em ", "Se apagará en ") \
  X(SleepSeconds, " s", " s", " s") \
  X(SleepAdd, "+15 min", "+15 min", "+15 min") \
  X(SleepShuttingDown, "Shutting down…", "Desligando…", "Apagando…") \
  X(SleepError, "Could not update sleep timer. Try again.", "Não deu para alterar o timer. Tente de novo.", "No se pudo cambiar el temporizador. Inténtalo de nuevo.") \
  X(SleepApply, "Apply", "Aplicar", "Aplicar") \
  X(PlayerOptions, "Options", "Opções", "Opciones") \
  X(PlayerTimeline, "Timeline", "Trilha do vídeo", "Barra de reproducción") \
  X(PlayerQuality, "Quality", "Qualidade", "Calidad") \
  X(PlayerChannel, "Go to channel", "Ir para o canal", "Ir al canal") \
  X(PlayerPaused, "Paused", "Pausado", "En pausa") \
  X(PlayerSeekWait, "Seeking is available after the video starts", "Você pode pular depois que o vídeo começar", "Podrás avanzar cuando comience el video") \
  X(PlayerLiveSeek, "Live broadcast: seeking is unavailable", "Transmissão ao vivo: não dá para pular", "No se puede avanzar en esta transmisión en vivo") \
  X(Captions, "Captions", "Legendas", "Subtítulos") \
  X(CaptionsOn, "On", "Ligadas", "Activados") \
  X(CaptionsOff, "Off", "Desligadas", "Desactivados") \
  X(CaptionsAuto, "Automatic", "Automáticas", "Automáticos") \
  X(CaptionsManual, "Creator captions", "Do criador", "Del creador") \
  X(CaptionsLoading, "Finding captions…", "Buscando legendas…", "Buscando subtítulos…") \
  X(CaptionsNone, "This video has no captions", "Este vídeo não oferece legendas", "Este video no tiene subtítulos") \
  X(CaptionsLiveNone, "YouTube has no captions for this live broadcast", "O YouTube não oferece legendas nesta live", "YouTube no ofrece subtítulos para esta transmisión") \
  X(CaptionsFailed, "Could not load captions. Press A to retry.", "Não deu para carregar legendas. Aperte A para tentar de novo.", "No se pudieron cargar los subtítulos. Presiona A para reintentar.") \
  X(CaptionsLanguage, "Caption language", "Idioma das legendas", "Idioma de los subtítulos") \
  X(PlayerAlsoOff, "Also turn off", "Também desligar", "Apagar también") \
  X(PlayerTimerOn, "Active", "Ativo", "Activo") \
  X(CaptionsLiveClock, "Captions for this broadcast are unavailable in the player", "As legendas desta live estão indisponíveis no player", "Los subtítulos de esta transmisión no están disponibles en el reproductor") \
  X(Spanish, "Español", "Español", "Español") \
  X(LargeText, "Large text", "Texto grande", "Texto grande") \
  X(Enabled, "Enabled", "Ativado", "Activado") \
  X(LargeTextHelp, "Larger text and fewer columns. Always enabled on SD TVs.", "Textos maiores e menos colunas. Sempre ativo em TVs SD.", "Texto más grande y menos columnas. Siempre activo en TV SD.") \
  X(VideoFit, "Picture size", "Tamanho da imagem", "Tamaño de imagen") \
  X(VideoFitLetterbox, "Fit (letterbox)", "Ajustar (faixas)", "Ajustar (con bandas)") \
  X(VideoFitFill, "Fill (crop)", "Preencher (cortar)", "Llenar (recortar)") \
  X(DisplaySDHelp, "480i/p follows Xbox Display Settings. SD keeps a safe TV margin.", "480i/p segue o painel do Xbox. SD reserva margem segura na TV.", "480i/p sigue la pantalla de Xbox. SD deja un margen seguro en la TV.") \
  X(Two, "2 (4:3)", "2 (4:3)", "2 (4:3)") \
  X(Language, "Language", "Idioma", "Idioma") \
  X(English, "English", "English", "English") \
  X(Portuguese, "Português", "Português", "Português") \
  X(SignedOut, "Signed out", "Desconectado", "Sesión cerrada") \
  X(SignInAction, "Press A to sign in", "A para conectar", "Presiona A para iniciar sesión") \
  X(SignOutAction, "Press A to sign out", "A para desconectar", "Presiona A para cerrar sesión") \
  X(SignedInAs, "Connected as ", "Conectado como ", "Conectado como ") \
  X(Pending, "Waiting for sign-in", "Aguardando login", "Esperando el inicio de sesión") \
  X(SavedAutomatically, "Changes are saved automatically.", "As mudanças são salvas automaticamente.", "Los cambios se guardan automáticamente.") \
  X(SaveFailed, "Could not save settings on this device", "Não deu para salvar as configurações neste dispositivo", "No se pudo guardar la configuración en este dispositivo") \
  X(NoSpeaker, "No speaker configured on the server", "Nenhuma caixa configurada no servidor", "No hay un altavoz configurado en el servidor") \
  X(SpeakerChecking, "Checking for a speaker…", "Procurando uma caixa…", "Buscando un altavoz…") \
  X(SyncHelp, "Adjust in steps of 40 ms. Positive values advance the picture.", "Ajuste em passos de 40 ms. Valores positivos adiantam a imagem.", "Ajusta en pasos de 40 ms. Los valores positivos adelantan la imagen.") \
  X(ChooseRow, "Select", "Escolher", "Seleccionar") \
  X(ChangeValue, "Change", "Alterar", "Cambiar") \
  X(Open, "Open", "Abrir", "Abrir") \
  X(Three, "3", "3", "3") \
  X(Four, "4", "4", "4") \
  X(SignInTitle, "Sign in to YouTube", "Conectar ao YouTube", "Iniciar sesión en YouTube") \
  X(SignInStep, "On your phone or computer, open", "No celular ou computador, abra", "En tu teléfono o computadora, abre") \
  X(DeviceURL, "google.com/device", "google.com/device", "google.com/device") \
  X(EnterCode, "Enter this code", "Digite este código", "Ingresa este código") \
  X(LoginWaiting, "Waiting for Google confirmation…", "Aguardando a confirmação do Google…", "Esperando la confirmación de Google…") \
  X(LoginExpired, "Code expired. Press A to get a new one.", "O código expirou. Aperte A para gerar outro.", "El código venció. Presiona A para obtener otro.") \
  X(AccountError, "Could not update your account. Press A to retry.", "Não deu para atualizar a conta. Aperte A para tentar de novo.", "No se pudo actualizar la cuenta. Presiona A para reintentar.") \
  X(AccountUnavailable, "Account unavailable. Press A to retry.", "Conta indisponível. Aperte A para tentar de novo.", "Cuenta no disponible. Presiona A para reintentar.") \
  X(SignOutTitle, "Sign out of YouTube?", "Desconectar do YouTube?", "¿Cerrar sesión en YouTube?") \
  X(SignOutBody, "Your personal lists will be removed from this console.", "Suas listas pessoais deixarão de aparecer neste console.", "Tus listas personales dejarán de aparecer en esta consola.") \
  X(Confirm, "Confirm", "Confirmar", "Confirmar") \
  X(Cancel, "Cancel", "Cancelar", "Cancelar") \
  X(SignedOutToast, "Signed out of YouTube", "Conta do YouTube desconectada", "Se cerró la sesión de YouTube") \
  X(Library, "Library", "Biblioteca", "Biblioteca") \
  X(Channel, "Channel", "Canal", "Canal") \
  X(GoChannel, "Go to channel", "Ir para o canal", "Ir al canal") \
  X(Subscribe, "Subscribe", "Inscrever-se", "Suscribirse") \
  X(Subscribed, "Subscribed", "Inscrito", "Suscrito") \
  X(Videos, "Videos", "Vídeos", "Videos") \
  X(Playlists, "Playlists", "Playlists", "Listas de reproducción") \
  X(PlayAll, "Play all", "Tocar tudo", "Reproducir todo") \
  X(QuickPicks, "Quick picks", "Escolhas rápidas", "Selecciones rápidas") \
  X(Charts, "Charts", "Paradas", "Listas de éxitos") \
  X(Mixes, "Mixes", "Mixes", "Mixes") \
  X(Songs, "Songs", "Músicas", "Canciones") \
  X(Albums, "Albums", "Álbuns", "Álbumes") \
  X(Artists, "Artists", "Artistas", "Artistas") \
  X(Artist, "Artist", "Artista", "Artista") \
  X(Album, "Album", "Álbum", "Álbum") \
  X(Radio, "Start radio", "Iniciar rádio", "Iniciar radio") \
  X(NowPlaying, "Now playing", "Tocando agora", "Reproduciendo ahora") \
  X(Queue, "Up next", "A seguir", "A continuación") \
  X(NextTrack, "Next track", "Próxima faixa", "Siguiente pista") \
  X(Lyrics, "Lyrics", "Letra", "Letra") \
  X(NoLyrics, "Lyrics are not available for this track.", "Letra indisponível para esta faixa.", "La letra de esta canción no está disponible.") \
  X(ShowVideo, "Show video", "Mostrar vídeo", "Mostrar video") \
  X(CardMenu, "Video options", "Opções do vídeo", "Opciones del video") \
  X(AddedLater, "Saved to Watch later", "Salvo em Assistir mais tarde", "Guardado en Ver más tarde") \
  X(AccountRequired, "Sign in to YouTube in Settings to save and subscribe.", "Conecte sua conta em Configurações para salvar e se inscrever.", "Inicia sesión en Configuración para guardar y suscribirte.") \
  X(ActionFailed, "Could not save. Press A to retry.", "Não foi possível salvar. Aperte A para tentar de novo.", "No se pudo guardar. Presiona A para reintentar.") \
  X(Saving, "Saving…", "Salvando…", "Guardando…") \
  X(StartingQueue, "Preparing the queue…", "Preparando a fila…", "Preparando la cola…") \
  X(History, "History", "Histórico", "Historial") \
  X(MusicSearch, "Search songs, albums and artists", "Buscar músicas, álbuns e artistas", "Buscar canciones, álbumes y artistas") \
  X(NoLive, "No live streams on this channel.", "Este canal não tem transmissões ao vivo.", "Este canal no tiene transmisiones en vivo.") \
  X(MoreLoading, "Loading the next page…", "Carregando a próxima página…", "Cargando la siguiente página…") \
  X(Results, "Search results", "Resultados da busca", "Resultados de búsqueda") \
  X(AllResults, "All results", "Tudo", "Todos") \
  X(MoreOptions, "Options", "Opções", "Opciones") \
  X(ChooseTab, "Choose a tab", "Escolher aba", "Elegir pestaña") \
  X(MusicIntro, "Your next favourite sound.", "Seu próximo som favorito.", "Tu próxima canción favorita.") \
  X(QueueLimit, "Playlists play up to 256 tracks.", "As playlists tocam até 256 faixas.", "Las listas reproducen hasta 256 pistas.") \
  X(SubscribeConfirm, "Unsubscribe from this channel?", "Cancelar a inscrição neste canal?", "¿Cancelar la suscripción a este canal?") \
  X(GoLive, "Jump to live", "Ir para o ao vivo", "Ir al directo") \
  X(ChannelLive, "Live", "Ao vivo", "En vivo") \
  X(MusicSpeakerFallback, "Speaker did not respond: sound on the TV.", "A caixa não respondeu: som na TV.", "El altavoz no responde: sonido en la TV.") \
  X(BtnA, "A", "A", "A") X(BtnB, "B", "B", "B") X(BtnX, "X", "X", "X") X(BtnY, "Y", "Y", "Y") \
  X(BtnView, "View", "View", "View") X(BtnMenu, "Menu", "Menu", "Menu") \
  X(BtnShoulders, "LB RB", "LB RB", "LB RB") X(BtnTriggers, "LT RT", "LT RT", "LT RT") \
  X(BtnHorizontal, "< >", "< >", "< >") X(BtnVertical, "↑ ↓", "↑ ↓", "↑ ↓")

enum class Key {
#define TEXT_KEY(k, en, pt, es) k,
  TUBE360_TEXT(TEXT_KEY)
#undef TEXT_KEY
  Count
};
inline constexpr const char* texts[][3] = {
#define TEXT_VALUE(k, en, pt, es) {en, pt, es},
  TUBE360_TEXT(TEXT_VALUE)
#undef TEXT_VALUE
};
#undef TUBE360_TEXT
static_assert(sizeof(texts) / sizeof(texts[0]) == (size_t)Key::Count);
inline const char* get(Lang lang, Key key) { return texts[(size_t)key][lang == Lang::PT ? 1 : lang == Lang::ES ? 2 : 0]; }
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
