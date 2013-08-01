Example usage for snapshot capture mode:

  ./Torikuru --session=ssled://user1@nylabads2/IDN_RDF \
             --output-path=output.dmp \
             --disable-update \
             --terminate-on-sync \
             --symbol-path=rics

Example usage for update capture mode:

  ./Torikuru --session=ssled://user1@nylabads2/IDN_RDF \
             --output-path=output.dmp \
             --disable-refresh \
             --time-limit=600 \
             --symbol-path=rics

Example usage for extraction mode:

  ./Torikuru --session=ssled://user1@nylabads2/IDN_RDF \
             --input-path=output.dmp \
             --output-path=\$1.csv


Long form of session declaration:

  --session="SSLED://user1@nylabads2:8101/IDN_RDF?application-id=256&instance-id=Instance1&position=127.0.0.1/net"

This will connect with the following parameters:

        Protocol: SSLED
        Username: user1
            Host: nylabds2
            Port: 8101
         Service: IDN_RDF
  Application Id: 256
     Instance ID: Instance1
        Position: 127.0.0.1/net


Verbose logging can be enabled with the --v=<log level> parameter, e.g.

  ./Torikuru --v=10

Using the --vmodule switch gives the per-module maximum logging levels, e.g.

  ./Torikuru --vmodule=torikuru=1

Wildcards are permitted for convenience referencing multiple modules, e.g.

  ./Torikuru --vmodule=tori*=1,con*=2
