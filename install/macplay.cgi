#!/bin/bash

echo "Content-type: text/html"
echo ""
OUTPUT="$(date)"
echo "Now is $OUTPUT <br>"
echo "Current directory is $(pwd) <br>"
echo "Shell Script name is $0 <br>"
argument=`echo "$QUERY_STRING" | sed "s|q=||"` 
echo "QUERY_STRING is: <b> $QUERY_STRING </b> <br>"
echo "Actual argument is: <b> $argument     </b> <br>"

/usr/local/sbin/macplay "$argument"

echo "</body></html>"
