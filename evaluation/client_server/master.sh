#!/bin/bash 

# Function to print script usage
usage(){
    echo -e "Usage: $0 opt remote [plotCommand debug tmp]"
    echo -e "opt = {(0) (1) (2) (3) (4) (5) (6) (7) (8)}"
    echo -e "\t(0) Pull new code and compile"
	echo -e "\t(1) Handshake duration (not used)" 
	echo -e "\t(2) Time to first byte f(no. slices)"
	echo -e "\t(3) Time to first byte f(delay)"
	echo -e "\t(4) Time to byte as a function of the number of proxies"
	echo -e "\t(5) Download time as a function of the file size"
	echo -e "\t(6) Download time in browser-like mode -- CDF"
	echo -e "\t(7) Number of connections per second"
	echo -e "\t(8) Byte overhead -- X axis is a few discrete scenarios"
	echo -e "remote = {(0) local experiments (1) Amazon experiments}"
	echo -e "run    = {(1) run experiment, (0) no run just plot"
	echo -e "----------------------------------OPTIONAL-----------------------------------------------"
    echo -e "[plotCommand = {matlab, myplot, none, ...} add your own to the script (default is no plotting)]"
    echo -e "[debug =  {(0) OFF (1) ON (instead of running just prints commands used)}]"
    echo -e "[tmp   =  {(0) OFF (1) ON (use tmp resulst folder)}]"
	exit 0
}
	
# Function to print script usage
tcpTrick(){
	if [ $proto == "spp_mod" ] 
	then 
		rwnd=10
		cwnd=10
		echo "Changing initrwnd to $rwnd and initcwnd to $cwnd"
		sudo ip route change 127.0.0.1 dev lo  proto static initrwnd $rwnd initcwnd $cwnd
		ip route show 
	fi
}

# Set of checks for correctness
[[ $# -lt 3 ]] && usage

# Parameters
if [ $# -ge 6 ]
then 
	tmp=$6
else
	tmp=0
fi

# result folder 
if [ $tmp -eq 1 ] 
then
	resFolder="../results/tmp"  
else
	resFolder="../results/tmp"  
fi 

matlabFolder="../results" # matlab folder 
R=50                      # number of repetitions
S_max=16                  # max number of slices 
rate=1                    # common rate
maxRate=8                 # max rate with no traffic
delay=20                  # delay 
iface="lo"                # interface
log="log_script"          # log file 
logCompile="log_compile"  # log file 
opt=$1                    # user choice for experiment
remote=$2                 # user choice, local or Amazon exp
parallel=0                # parallel experiment (not used here but needed for plotting)
RUN_EXP=$3                # run experiment or not 
debug=0                   # no debugging by default
plotCommand="none"        # Usere selection for plotting 
protoList[1]="ssl"        # array for protocol types currently supported
protoList[2]="fwd"
protoList[3]="spp"
protoList[4]="pln"     
protoList[5]="spp_mod"     
key="amazon.pem"          # amazon key 
user="ubuntu"             # amazon user 

# folder for compilations
remoteFolder="./secure_proxy_protocol" 
localFolder=$HOME"WorkTelefonica/HTTP-2/sigcomm_evaluation/secure_proxy_protocol"

# derive proto size 
proto_count=${#protoList[@]}

# read user plot input if provided
if [[ $# -ge 4 ]]
then 
	plotCommand=$4
fi
# instead of running just print commands
if [ $# -ge 5 ]
then 
	debug=$5                  
fi

#cleanup 
if [ -f $log ]
then 
	rm -v $log 
fi

# check key exhists (for remote exp)
if [ $remote -eq 1 ] 
then 
	if [ ! -f $key ] 
	then 
		echo "Amazon ssh key is missing (<<$key>>)"
		exit 0 
	fi
fi
# Definition for logging purpose 
if [ $remote -eq 1 ] 
then 
	adj="Remote (Amazon)"
else
	adj="Local"
fi

#echo "TCP INIT CWND"
#cat /usr/src/linux-headers-3.13.0-39-generic/include/net/tcp.h | grep -A 2 initcwnd
#echo "TCP INIT RWND"
#cat /usr/src/linux-headers-3.13.0-39-generic/include/net/tcp.h | grep -A 2 initrwnd
	
# no run if u only want to plot 
if [ $RUN_EXP -eq 1 -o $opt -eq 0 ]
then
# switch on user selection 
	case $opt in 
	0)
		machineFile="machines"
		count=0
		echo "[MASTER] Compilation of last version STARTED"
		if [ $remote -eq 0 ] 
		then
			echo "[MASTER] Pull code (git) and recompile at local machine (check your path. Current path is <<$localFolder>>!!!!)"
			cd $localFolder
			git pull
			make
			sudo make install_sw
			cd evaluation/client_server
			make clean
			make
			cd - 
		else
			echo "[MASTER] Pull code (git) and recompile at machine in file <<$machinesFile>>"
			if [ ! -f $machineFile ] 
			then 
				echo "[MASTER] ERROR! File <<$machinesFile>> is missing"
				exit 0 
			fi
			for line in `cat $machineFile`
			do
				comm="cd $remoteFolder; git fetch --all; git reset --hard origin/master; make clean; ./config; make; sudo make install_sw; cd evaluation/client_server; make clean; make"
				command="script -q -c '"$comm"'"         # Make typescript version of the command (solve SUDO problem via SSH)
				addr=`echo $line | cut -f 2 -d "@" | cut -f 1 -d ":"`
				port=`echo $line | cut -f 2 -d "@" | cut -f 2 -d ":"`
				user=`echo $line | cut -f 1 -d "@"`
				echo "[MASTER] Working on machine <<$addr:$port>> (with user <<$user>>)"
				if [ $addr == "localhost" ]
				then
					continue
				fi
				if [ $addr == "tid.system-ns.net" ]
				then
		            ssh -o StrictHostKeyChecking=no -p $port $user@$addr "$command" >> $logCompile 2>&1 &
                else
		            ssh -o StrictHostKeyChecking=no -p $port -i $key $user@$addr "$command" >> $logCompile 2>&1 &
				fi            
			done
		fi	
		# check that compilation is done and ok 	
		if [ $remote -eq 0 ] 
		then 
			currTime=`date | awk '{print $2"_"$3"_"$4;}'`
			p="/usr/local/ssl/lib"
			echo "[MASTER] Checking for library at location <<$p>>"
			echo "[MASTER] Current time is $currTime."
			echo "[MASTER] Libraries were last compiled:"
			ls -lrth  $p | grep lib | awk '{print "\t" $NF ": "$6"_"$7"_"$8}'
		else
			active=`ps aux | grep ssh | grep make | grep script | grep -v grep | wc -l`
			while [ $active -gt 0 ] 
			do 
				echo "[MASTER] Still $active compilation running remotely"
				active=`ps aux | grep ssh | grep make | grep script | grep -v grep | wc -l`
				sleep 10
			done
			count=0
			for line in `cat $machineFile`
			do
				command="cd $remoteFolder; cd evaluation/client_server; ./checkLibrary.sh"
				addr=`echo $line | cut -f 2 -d "@" | cut -f 1 -d ":"`
				port=`echo $line | cut -f 2 -d "@" | cut -f 2 -d ":"`
				user=`echo $line | cut -f 1 -d "@"`
				echo "[MASTER] Checking machine <<$addr:$port>> (with user <<$user>>)"
				if [ $addr == "localhost" ]
				then
					continue
				fi
				if [ $addr == "tid.system-ns.net" ]
				then
		            ssh -o StrictHostKeyChecking=no -p $port $user@$addr "$command" 
                else
		            ssh -o StrictHostKeyChecking=no -p $port -i $key $user@$addr "$command"
				fi 
			done
		fi
		
		# all good, just exit 
		echo "[MASTER] Compilation of last version COMPLETED"
		exit 0
		;;
	1) 
		echo "[MASTER] Option $opt currently not supported"
		exit 0
		;;
	2)
		echo "[MASTER] $adj analysis of first time to byte as a function of number of slices (check <<$log>> for experiment progress)"
		for ((i=1; i<=proto_count; i++))
		do
			proto=${protoList[$i]}
			echo -e "\t[MASTER] Working on protocol $proto (Running <<$R>> tests per configuration)"
			if [ $remote -eq 0 ]
			then
				if [ $debug -eq 1 ] 
				then
					echo "./perf_script.sh $S_max $R $proto $opt $remote $rate $maxRate $delay $iface >> $log"
				else
					./perf_script.sh $S_max $R $proto $opt $remote $rate $maxRate $delay $iface >> $log 2>/dev/null
				fi
			else
				if [ $debug -eq 1 ] 
				then
					echo "./perf_script.sh $S_max $R $proto $opt $remote >> $log"
				else
					./perf_script.sh $S_max $R $proto $opt $remote >> $log 2>/dev/null
				fi
			fi
		done
			;;

	3) 
		echo "[MASTER] $adj analysis of first time to byte as a function of latency"
		S_max=4
		for ((i=1; i<=proto_count; i++))
		do
			proto=${protoList[$i]}
			echo -e "\t[MASTER] Working on protocol $proto ..."

			# run analysis
			if [ $debug -eq 1 ] 
			then
				echo "./perf_script.sh $S_max $R $proto $opt $remote $rate $maxRate $delay $iface >> $log 2>/dev/null"
			else
				./perf_script.sh $S_max $R $proto $opt $remote $rate $maxRate $delay $iface >> $log 2>/dev/null
			fi
		done
		;;

	4) 
		echo "[MASTER] $adj analysis of first time to byte as a function of the number of proxies"
		S_max=4
		for ((i=1; i<=proto_count; i++))
		do
			proto=${protoList[$i]}
			echo -e "\t[MASTER] Working on protocol $proto ..."
			
			# run analysis
			if [ $debug -eq 1 ] 
			then
				echo "./perf_script.sh $S_max $R $proto $opt $remote $rate $maxRate $delay $iface >> $log 2>/dev/null"
			else
				./perf_script.sh $S_max $R $proto $opt $remote $rate $maxRate $delay $iface >> $log 2>/dev/null
			fi
		done
		;;
	
	5) 
		echo "[MASTER] $adj analysis of download time as a function of the file size"
		echo "!!![MASTER] Increasing transfer rate to 20Mbps and lowering repetitions to just 10 (for testing)!!!"
		#----------------
		rate=10
		maxRate=10
		R=10
		#----------------
		S_max=4
		for ((i=1; i<=proto_count; i++))
		do
			proto=${protoList[$i]}
			echo -e "\t[MASTER] Working on protocol $proto ..."
			
			# deal with SPP_MOD
			tcpTrick
			
			# run analysis
			if [ $debug -eq 1 ] 
			then
				echo "./perf_script.sh $S_max $R $proto $opt $remote $rate $maxRate $delay $iface"
			else
				./perf_script.sh $S_max $R $proto $opt $remote $rate $maxRate $delay $iface >> $log 2>/dev/null
			fi
		done
		;;
	
	6) 
		echo "[MASTER] $adj analysis of download time in browser-like mode"
		echo "!!![MASTER] Using only 10 repetitions (for testing)!!!"
		#----------------
		R=10
		#----------------
		S_max=4
		for ((i=1; i<=proto_count; i++))
		do
			proto=${protoList[$i]}
			echo -e "\t[MASTER] Working on protocol $proto ..."
			
			# run analysis
			if [ $debug -eq 1 ] 
			then
				echo "./perf_script.sh $S_max $R $proto $opt $rate $maxRate $delay $iface >> $log"
			else
				./perf_script.sh $S_max $R $proto $opt $remote $rate $maxRate $delay $iface >> $log 2>/dev/null
			fi
		done
		;;
	
	7) 
		echo "[MASTER] $adj analysis of number of connections per second"
		R=5
		S_max=16
		str="l($S_max)/l(2)"
		X=`echo $str | bc -l  | cut -f 1 -d "."`
		let "estTime = (R * X * 30) / 60)"
		for ((i=1; i<=proto_count; i++))
		do
			proto=${protoList[$i]}
			echo -e "\t[MASTER] Working on protocol $proto (30 second per parameter value and repetition. Est time $estTime minutes)"
			if [ $debug -eq 1 ] 
			then
				echo "./perf_script.sh $S_max $R $proto $opt $remote $rate $maxRate $delay $iface >> $log"
			else
				./perf_script.sh $S_max $R $proto $opt $remote $rate $maxRate $delay $iface >> $log 2>/dev/null
			fi
		done
		;;
		

	8) 
		echo "[MASTER] $adj analysis of byte overhead -- X axis is a few discrete scenarios"
		echo "[MASTER] NOTE: This test ignores network parameters"
		R=1  # byte overhead shouldn't vary
		for ((i=1; i<=proto_count; i++))
		do
			proto=${protoList[$i]}
			echo -e "\t[MASTER] Working on protocol $proto ..."
			
			# run analysis
			# TODO: use local/Amazon flag here once supported (instead of 0)
			if [ $debug -eq 1 ] 
			then
				echo "./perf_script.sh $S_max $R $proto $opt 0 >> $log 2>/dev/null"
			else
				./perf_script.sh $S_max $R $proto $opt 0 >> $log 2>/dev/null
			fi
		done
		;;

	esac
fi

# Plotting results 
if [ $plotCommand == "matlab" ] 
then 
	echo "[MASTER] Plotting results (option $opt)"
	echo "[MATLAB] Running MATLAB...(it takes some time at first launch)"

	if [ $opt -eq 7 ] 
	then 
		matlab -nodisplay -nosplash -r "cd $matlabFolder; plotSigcomm($opt, $remote, $parallel, 'client', $tmp); plotSigcomm($opt, $remote, $parallel, 'mbox', $tmp); plotSigcomm($opt, $remote, $parallel, 'server', $tmp);quit"
	else 
		echo "plotSigcomm($opt, $remote, $parallel, 'none', $tmp)"
		matlab -nodisplay -nosplash -r "cd $matlabFolder; plotSigcomm($opt, $remote, $parallel, 'none', $tmp); quit"
	fi

	# Generating summary report 
	cd ../results 
	../results/script.sh 
	cd - 
elif [ $plotCommand == "myplot" ]
then
	echo "[MASTER] Plotting results (option $opt)"
	cd ../results
	./plot.py $opt
	cd -
else 
	echo "[MASTER] No plotting requested or plotting type <<$plotCommand>> not supported"
fi
