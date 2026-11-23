"""Nest com Wi-Fi ruim: um cliente só, sem relançar o receptor depois da queda, e stop que chega quando a conexão volta."""
import importlib.util
import os
from pathlib import Path
import tempfile
import threading
import time
import unittest
from types import SimpleNamespace
from unittest.mock import patch

NS = "urn:x-cast:com.google.cast.media"


class FakeMedia:
    namespace = NS

    def __init__(self, cast):
        self.cast = cast
        self.status = SimpleNamespace(player_state="UNKNOWN", content_id=None, current_time=0, last_updated=None,
                                      idle_reason=None, media_session_id=None)
        self.calls = []

    def update_status(self):
        # a pychromecast relança o receptor quando o canal de mídia não está aberto: é isso que não pode acontecer
        if NS not in self.cast.socket_client.app_namespaces:
            self.cast.launches += 1
        self.calls.append("update_status")

    def stop(self):
        self.calls.append("stop")

    def pause(self):
        self.calls.append("pause")

    def play(self):
        self.calls.append("play")


class FakeCast:
    def __init__(self):
        self.socket_client = SimpleNamespace(is_connected=True, stop=threading.Event(), app_namespaces=[NS])
        self.status = SimpleNamespace(app_id="CC1AD845", volume_level=0.5)
        self.media_controller = FakeMedia(self)
        self.launches = 0
        self.disconnected = False

    def disconnect(self, timeout=None):
        self.disconnected = True

    def quit_app(self):
        pass

    def drop_wifi(self):
        self.socket_client.is_connected = False
        self.socket_client.app_namespaces = []  # é o que a pychromecast faz em initialize_connection
        self.media_controller.status.player_state = "UNKNOWN"
        self.media_controller.status.content_id = None


class NestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        with patch.dict(os.environ, HOME=cls.tmp.name, TUBE360_CONFIG=str(Path(__file__).with_name('config.example.json')),
                        TUBE360_ACCOUNT_URL='http://127.0.0.1:1'):
            spec = importlib.util.spec_from_file_location('nest_test_server', Path(__file__).with_name('tube360_server.py'))
            cls.m = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(cls.m)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.m.remote = self.m.Remote()
        self.nest = self.m.Nest()
        self.cast = FakeCast()
        self.nest.cc = self.cast

    def playing(self):
        n, st = self.nest, self.cast.media_controller.status
        n.active, n.video_id, n.token, n.t0 = True, "abcdefghijk", "s=1", 10.0
        st.player_state, st.content_id, st.current_time = "PLAYING", "http://x/api/audio/abcdefghijk?t=10&s=1", 5.0
        self.m.remote.last_poll, self.m.remote.playing = time.monotonic(), "abcdefghijk"

    def test_queda_de_wifi_nao_relanca_o_receptor_nem_vira_idle(self):
        self.playing()
        time.sleep(0.6)
        self.assertEqual(self.nest.state, "PLAYING")
        self.cast.drop_wifi()
        time.sleep(0.8)
        self.cast.socket_client.is_connected = True  # reconectou, mas o canal de mídia ainda não voltou
        time.sleep(0.8)
        self.assertEqual(self.cast.launches, 0)
        self.assertEqual(self.nest.state, "PLAYING")
        self.cast.socket_client.app_namespaces = [NS]  # canal de volta, status ainda zerado
        time.sleep(0.6)
        self.assertEqual(self.cast.launches, 0)
        self.assertEqual(self.nest.state, "PLAYING")

    def test_stop_sem_conexao_repete_quando_volta(self):
        self.playing()
        self.cast.drop_wifi()
        t0 = time.monotonic()
        self.nest.command("stop")
        self.assertLess(time.monotonic() - t0, 0.5, "stop não pode segurar a trava esperando o Wi-Fi")
        self.assertTrue(self.nest.want_stop)
        self.assertFalse(self.nest.active)
        self.assertNotIn("stop", self.cast.media_controller.calls)
        self.cast.socket_client.is_connected = True
        self.cast.socket_client.app_namespaces = [NS]
        self.cast.media_controller.status.media_session_id = 7
        deadline = time.monotonic() + 3
        while "stop" not in self.cast.media_controller.calls and time.monotonic() < deadline:
            time.sleep(0.1)
        self.assertIn("stop", self.cast.media_controller.calls)
        self.assertFalse(self.nest.want_stop)
        self.assertEqual(self.cast.launches, 0)

    def test_stop_com_outro_app_no_nest_nao_relanca(self):
        self.cast.status.app_id = "89EA58C1"
        self.cast.socket_client.app_namespaces = []
        self.nest.command("stop")
        self.assertFalse(self.nest.want_stop)
        self.assertEqual(self.cast.launches, 0)

    def test_connect_espera_o_cliente_que_reconecta_em_vez_de_abrir_outro(self):
        self.cast.drop_wifi()
        threading.Timer(0.5, lambda: setattr(self.cast.socket_client, "is_connected", True)).start()
        with patch.object(self.m.pychromecast, "get_chromecast_from_cast_info", side_effect=AssertionError("abriu outro")):
            self.assertIs(self.nest.connect(), self.cast)
        self.assertFalse(self.cast.disconnected)

    def test_connect_desliga_o_cliente_morto_antes_de_abrir_outro(self):
        self.cast.socket_client.stop.set()
        novo = FakeCast()
        novo.wait = lambda timeout=None: None
        with patch.dict(self.m.CONFIG, nest={"host": "192.0.2.1"}), \
                patch.object(self.m.pychromecast, "get_chromecast_from_cast_info", return_value=novo):
            self.assertIs(self.nest.connect(), novo)
        self.assertTrue(self.cast.disconnected)


if __name__ == "__main__":
    unittest.main()
