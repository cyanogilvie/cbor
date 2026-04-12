# vim: ft=tcl foldmethod=marker foldmarker=<<<,>>> ts=4 shiftwidth=4

set here	[file dirname [file normalize [info script]]]
tcl::tm::path add [file join $here ../teabase]

package require teabase_bench


proc main {} {
	try {
		set here	[file dirname [file normalize [info script]]]
		puts "[string repeat - 80]\nStarting benchmarks\n"
		teabase_bench::run_benchmarks $here {*}$::argv
	} on ok {} {
		exit 0
	} trap {BENCH BAD_RESULT} {errmsg options} {
		puts stderr $errmsg
		exit 1
	} trap {BENCH BAD_CODE} {errmsg options} {
		puts stderr $errmsg
		exit 1
	} trap {BENCH INVALID_ARG} {errmsg options} {
		puts stderr $errmsg
		exit 1
	} trap exit code {
		exit $code
	} on error {errmsg options} {
		puts stderr "Unhandled error from benchmark_mode: [dict get $options -errorinfo]"
		exit 2
	}
}

main

# vim: ft=tcl foldmethod=marker foldmarker=<<<,>>> ts=4 shiftwidth=4
