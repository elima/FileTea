#!/bin/sh

### BEGIN INIT INFO
# Provides:          filetea
# Required-Start:    $syslog
# Required-Stop:     $syslog
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: Start FileTea server daemon at boot time
# Description:       Enable FileTea server daemon to run automatically on system startup.
### END INIT INFO

DAEMON="/opt/jhbuild/build/evd/sbin/filetea"
CONFIG_FILE="/opt/jhbuild/build/evd/etc/filetea/filetea-tls-devel.conf"
PID_FILE="/var/run/filetea.pid"

case "$1" in
  start)
    echo "Starting FileTea server daemon"
    $DAEMON -c $CONFIG_FILE -D
    ;;
  stop)
    echo "Stopping FileTea server daemon"
    kill `cat $PID_FILE`
    ;;
  *)
    echo "Usage: /etc/init.d/filetea {start|stop}"
    exit 1
    ;;
esac

exit 0
