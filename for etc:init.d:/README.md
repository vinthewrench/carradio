install

sudo nano /etc/init.d/carradio

---

```bash
 #!/bin/bash
# /etc/init.d/fan

### BEGIN INIT INFO

# Provides:carradio
# Required-Start:$remote_fs $syslog
# Required-Stop:$remote_fs $syslog
# Default-Start:2 3 4 5
# Default-Stop:0 1 6
# Short-Description: carradio
# Description: carradio auto start after boot
### END INIT INFO

case "$1" in
    start)
        echo "Starting carradio"
        cd /home/vinthewrench/carradio/bin
        nohup /home/vinthewrench/carradio/bin/carradio &
         ;;
    stop)
        echo "Stopping carradio"
     	 killall -9 carradio
         ;;
    *)
        echo "Usage: service carradio start|stop"
        exit 1
        ;;
esac
exit 0

```

sudo chmod +x /etc/init.d/carradio
sudo update-rc.d carradio defaults

To remove :

sudo update-rc.d  -f carradio remove

sudo service carradio start # start the service
sudo service carradio stop # stop the service
sudo service carradio restart # restart the service
sudo service carradio status # check service status