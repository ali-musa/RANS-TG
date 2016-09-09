#!/usr/bin/python
import sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pylab as pl
from mpltools import style

#plot settings
legend_pos = (0.5,-0.2)
style.use('ieee.transaction')
# pl.rcParams['lines.linewidth'] = 2
# pl.rcParams['font.weight']="large"
# pl.rcParams['legend.loc'] = 'best'
# pl.rcParams['legend.set_bbox_to_anchor'] = (1,0.5)
pl.rc('legend', loc='upper center')#, bbox_to_anchor=(1, 0.5))#, color='r')
# pl.rcParams['legend.fancybox']=True#, shadow=True
# pl.rcParams['legend.bbox_to_anchor']=(1, 0.5)
# pl.rcParams['legend.bbox_to_anchor']=(1, 0.5)
# pl.rcParams['bbox_to_anchor']=(1, 0.5)
# pl.legend(bbox_to_anchor=(1, 0.5))
markerslist=["o","v","^","s","*","D","p","<", ">", "H", "1", "2","3", "4"]
# markerslist=["o","o","v","v","^","^","s","s","*","*","D","D","p","p","<","<"]
# markerslist=["x","x","x","x","x"]
pl.rcParams['savefig.dpi']=300


def plot_graphs(input_paths, labels, outputfile, xlabel, ylabel, title):
	global markerslist, chunk_size, file_size_distribution, queue_limit
	fig=pl.figure()
	local_marker_list = markerslist[:]
	loads_array=[]
	y_array=[]
	label_array=[]
	for file in input_paths:
		loads = []
		y = []
		with open(file, 'r') as csvfile:
			for line in csvfile:
				lineList = line.split(",")
				loads.append(lineList[0])
				y.append(lineList[1].split("\n")[0])

		if labels != []:
			lab = labels.pop(0)
		else:
			lab=file.split("/")[-1]
		pl.plot(loads, y, label=lab,marker=local_marker_list.pop(0))
		loads_array.append(loads)
		y_array.append(y)
		label_array.append(lab)


	lg = pl.legend(bbox_to_anchor=legend_pos)#loc='best', fancybox=True)#, shadow=True)
	lg.draw_frame(True)
	# lg = pl.legend(loc='center left', bbox_to_anchor=(1, 0.5))
	# lg.draw_frame(True)
	# pl.title("64MB chunks, 1Gbps links, 10 servers")	
	
	pl.title(title)
	# pl.text(-0.2,-0.2,"queue_limit: "+str(queue_limit), fontsize=8)
	pl.grid(True)
	

	# show the plot on the screen
	# pl.show()
	# directory=plot_path
	# if not os.path.exists(directory):
	# 	os.mkdir( directory );


	pl.xlabel(xlabel)
	pl.ylabel(ylabel)
		# # pl.yscale('log')
	fig.savefig(outputfile, bbox_inches='tight', transparent=False)

	pl.cla()   # Clear axis
	pl.clf()   # Clear figure
	pl.close() # Close a figure window
	return loads_array, y_array, label_array


def print_usage(prog_name):
		print "usage: "+ prog_name + " -f <filepath1> -l <label1> -f <filepath2> -l <label2> ... \n-o <outputfile> -x <xlabel> -y <ylabel>  -t <title>"

if __name__ == '__main__':
	input_paths=[]
	input_labels=[]
	outputfile="plot.png"
	xlabel=""
	ylabel=""
	title=""
	if len(sys.argv) > 2 and "-f" in sys.argv:
		iterator = xrange(1,len(sys.argv)).__iter__()
		for arg_index in iterator:
			if sys.argv[arg_index] == "-f":
				iterator.next()	
				arg_index+=1
				input_paths.append(sys.argv[arg_index])
			elif sys.argv[arg_index] == "-l":
				iterator.next()	
				arg_index+=1
				input_labels.append(sys.argv[arg_index])
			elif sys.argv[arg_index] == "-o":
				iterator.next()	
				arg_index+=1
				outputfile=sys.argv[arg_index]
			elif sys.argv[arg_index] == "-x":
				iterator.next()	
				arg_index+=1
				xlabel=sys.argv[arg_index]
			elif sys.argv[arg_index] == "-y":
				iterator.next()	
				arg_index+=1
				ylabel=sys.argv[arg_index]
			elif sys.argv[arg_index] == "-t":
				iterator.next()	
				arg_index+=1
				title=sys.argv[arg_index]
			else:
				print_usage(sys.argv[0])
				sys.exit()
	else:
		print_usage(sys.argv[0])
		sys.exit()

	plot_graphs(input_paths, input_labels, outputfile, xlabel, ylabel, title)
	
	print "Plotting complete\n************************************\n"
	