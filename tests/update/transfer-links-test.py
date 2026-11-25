#!/usr/bin/env python3
"""Web/API/raw/assets: redirects locais com curl, sem consultar o GitHub real."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import ssl
import subprocess
import tempfile
import threading

OLD='PedroHenrique0713'
NEW='revive360'
ASSET='https://release-assets.githubusercontent.com/github-production-release-asset/1/app.xex'


class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def do_GET(self):
        url='https://'+self.headers['Host']+self.path
        self.server.requests.append(url)
        body=b'';target=None
        if '/'+OLD+'/' in self.path:target=url.replace('/'+OLD+'/', '/'+NEW+'/')
        elif '/releases/download/' in self.path:target=ASSET
        elif self.headers['Host']=='api.github.com':body=json.dumps(dict(full_name=NEW+'/tube360',id=123)).encode()
        else:body=b'preserved fixture bytes'
        self.send_response(self.server.code if target else 200)
        if target:self.send_header('Location',target)
        self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)


def main():
    rows=[]
    with tempfile.TemporaryDirectory(prefix='tube360-transfer-links-') as temp:
        folder=Path(temp);cert=folder/'ca.pem';key=folder/'key.pem'
        subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-subj','/CN=local transfer test',
            '-addext','subjectAltName=DNS:github.com,DNS:api.github.com,DNS:raw.githubusercontent.com,DNS:release-assets.githubusercontent.com',
            '-days','1','-keyout',str(key),'-out',str(cert)],check=True,capture_output=True)
        server=ThreadingHTTPServer(('127.0.0.1',0),Handler);server.daemon_threads=True
        tls=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);tls.load_cert_chain(cert,key)
        server.socket=tls.wrap_socket(server.socket,server_side=True)
        thread=threading.Thread(target=server.serve_forever);thread.start()
        try:
            for code in (301,302,307,308):
                server.code=code
                for name,url in [('web','https://github.com/'+OLD+'/tube360'),
                    ('releases-web','https://github.com/'+OLD+'/tube360/releases'),
                    ('api-latest','https://api.github.com/repos/'+OLD+'/tube360/releases/latest'),
                    ('download','https://github.com/'+OLD+'/tube360/releases/download/v0.3.2/tube360.xex'),
                    ('raw','https://raw.githubusercontent.com/'+OLD+'/tube360/rules/rules.json')]:
                    for follow in (False,True):
                        server.requests=[]
                        cmd=['curl','--noproxy','*','--cacert',str(cert),'--connect-to',f'::127.0.0.1:{server.server_port}',
                             '--proto','=https','--proto-redir','=https','--max-redirs','5','--max-time','10','-sS',
                             '-o',str(folder/'body'),'-w','%{http_code} %{url_effective}']
                        if follow:cmd.append('-L')
                        response=subprocess.check_output([*cmd,url],text=True)
                        status,final=response.split(' ',1)
                        assert int(status)==(200 if follow else code),response
                        assert len(server.requests)==(3 if follow and name=='download' else 2 if follow else 1)
                        if follow and name=='api-latest':assert json.loads((folder/'body').read_bytes())['full_name']==NEW+'/tube360'
                        rows.append(dict(endpoint=name,redirect=code,follow=follow,status=int(status),final=final,requests=list(server.requests)))
        finally:server.shutdown();server.server_close();thread.join()
    output=Path('.work/f50/transfer-links.json');output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(rows,indent=2)+'\n')
    print('PASS 40 local TLS curl cases: web, releases, API, asset chain and raw; all 301/302/307/308; no GitHub requests')


if __name__=='__main__':main()
