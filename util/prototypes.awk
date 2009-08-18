BEGIN {
	print "/*\n * Prototypes\n */\n"
}

/^[^[:space:]#{}][^#(){}]+[^:;(]$/ {
	if($1 == "static") {
		rv = ""
		next
	}

	rv = $0

	if(rv !~ /\*[[:space:]]*$/) {
		rv = rv
	}
}

/^[^[:space:]#{}].*[^:;]$/ {
	if(length(rv) == 0) {
		next
	}

	if($0 ~ /\(/) {
		funcstr = $0

		if($0 ~ /\)/) {
			print rv, funcstr ";"
			rv = ""
			next
		}

		while($0 !~ /\)[[:space:]]*$/) {
			getline
			sub(/^[[:space:]]*/, "")
			if($0 !~ /\)[[:space:]]*/) {
				sub(/[[:space:],]*$/, ", ")
			}
			funcstr = funcstr $0
		}

		print rv, funcstr ";"

		rv = ""
	}
}
