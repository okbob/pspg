for i in {1..100}
do
	echo `date`
	echo
	cat << END
┌──────────┬──────────┐
│ ?column? │ ?column? │
╞══════════╪══════════╡
│        1 │        2 │
│        1 │        2 │
└──────────┴──────────┘
(2 rows)
END

	echo
	echo -n -e '\x05'

sleep 1

done;